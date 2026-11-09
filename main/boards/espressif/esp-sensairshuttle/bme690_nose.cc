#include "bme690_nose.h"

#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <cmath>
#include <cstring>

namespace {
constexpr char TAG[] = "Bme690Nose";
constexpr uint32_t kPeriodMs = 3000;
constexpr uint16_t kHeaterMs = 100;
constexpr int64_t kFreshUs = 10000000;
}  // namespace

void Bme690Nose::Start(i2c_master_bus_handle_t bus) {
    if (task_ || !bus)
        return;
    bus_ = bus;
    if (xTaskCreate([](void* arg) { static_cast<Bme690Nose*>(arg)->Run(); }, "bme690_nose", 4096,
                    this, 1, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "Task allocation failed: stack=4096B; voice/display unaffected");
    }
}

BME69X_INTF_RET_TYPE Bme690Nose::Read(uint8_t reg, uint8_t* data, uint32_t len, void* context) {
    auto* self = static_cast<Bme690Nose*>(context);
    return self->device_ && data && len &&
                   i2c_master_transmit_receive(self->device_, &reg, 1, data, len, 100) == ESP_OK
               ? BME69X_INTF_RET_SUCCESS
               : -1;
}

BME69X_INTF_RET_TYPE Bme690Nose::Write(uint8_t reg, const uint8_t* data, uint32_t len,
                                       void* context) {
    auto* self = static_cast<Bme690Nose*>(context);
    uint8_t buffer[64];
    if (!self->device_ || !data || !len || len > sizeof(buffer) - 1)
        return -1;
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    return i2c_master_transmit(self->device_, buffer, len + 1, 100) == ESP_OK
               ? BME69X_INTF_RET_SUCCESS
               : -1;
}

void Bme690Nose::Delay(uint32_t period_us, void*) {
    // Bosch delays run only in our worker. Long heater waits yield the CPU;
    // short register-settling delays avoid rounding below the requested time.
    if (period_us >= 1000) {
        const uint32_t tick_us = portTICK_PERIOD_MS * 1000;
        vTaskDelay((period_us + tick_us - 1) / tick_us);
    } else {
        esp_rom_delay_us(period_us);
    }
}

bool Bme690Nose::Initialize() {
    uint8_t address = BME69X_I2C_ADDR_LOW;
    esp_err_t err = i2c_master_probe(bus_, address, 100);
    if (err != ESP_OK) {
        address = BME69X_I2C_ADDR_HIGH;
        err = i2c_master_probe(bus_, address, 100);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "No BME690 at 0x76/0x77: %s; original Shuttle: 3.3V SDA=2 SCL=3 "
                 "CS=10 high SDO=9 low. Retry in 10s",
                 esp_err_to_name(err));
        return false;
    }
    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = 100000;
    err = i2c_master_bus_add_device(bus_, &device_config, &device_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Add device failed: %s", esp_err_to_name(err));
        return false;
    }
    sensor_ = {};
    sensor_.intf = BME69X_I2C_INTF;
    sensor_.read = Read;
    sensor_.write = Write;
    sensor_.delay_us = Delay;
    sensor_.intf_ptr = this;
    sensor_.amb_temp = 25;
    int8_t result = bme69x_init(&sensor_);
    {
        std::lock_guard lock(mutex_);
        snapshot_.address = address;
        snapshot_.chip_id = sensor_.chip_id;
        snapshot_.variant_id = sensor_.variant_id;
        snapshot_.detected = result == BME69X_OK && sensor_.chip_id == BME69X_CHIP_ID &&
                             sensor_.variant_id == BME690_VARIANT_GAS_HIGH;
    }
    // 0x61 alone also identifies other BME68x parts. Verify BME690 variant=2.
    if (result != BME69X_OK || sensor_.chip_id != BME69X_CHIP_ID ||
        sensor_.variant_id != BME690_VARIANT_GAS_HIGH) {
        ESP_LOGE(TAG, "Not initialized as BME690: result=%d chip=0x%02x variant=%lu", result,
                 sensor_.chip_id, static_cast<unsigned long>(sensor_.variant_id));
        Disconnect();
        return false;
    }
    if (!self_test_attempted_) {
        self_test_attempted_ = true;
        result = bme69x_selftest_check(&sensor_);
        {
            std::lock_guard lock(mutex_);
            snapshot_.self_test = result;
        }
        ESP_LOGI(TAG, "Bosch self-test: %s result=%d", result == BME69X_OK ? "PASS" : "FAIL",
                 result);
        // The self-test changes measurement/heater configuration; restore it.
        result = bme69x_init(&sensor_);
    }
    config_ = {};
    config_.filter = BME69X_FILTER_OFF;
    config_.odr = BME69X_ODR_NONE;
    config_.os_hum = BME69X_OS_16X;
    config_.os_pres = BME69X_OS_1X;
    config_.os_temp = BME69X_OS_2X;
    if (result == BME69X_OK)
        result = bme69x_set_conf(&config_, &sensor_);
    bme69x_heatr_conf heater{};
    heater.enable = BME69X_ENABLE;
    heater.heatr_temp = 300;
    heater.heatr_dur = kHeaterMs;
    if (result == BME69X_OK)
        result = bme69x_set_heatr_conf(BME69X_FORCED_MODE, &heater, &sensor_);
    if (result != BME69X_OK) {
        ESP_LOGE(TAG, "Configuration failed: result=%d", result);
        Disconnect();
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.configured = true;
    }
    ESP_LOGI(TAG,
             "Configured: chip=0x%02x variant=%lu addr=0x%02x I2C=100kHz "
             "forced/3s heater=300C/100ms stack=4096B; raw gas only, no BSEC/IAQ",
             sensor_.chip_id, static_cast<unsigned long>(sensor_.variant_id), address);
    return true;
}

bool Bme690Nose::Measure() {
    int8_t result = bme69x_set_op_mode(BME69X_FORCED_MODE, &sensor_);
    if (result != BME69X_OK)
        return false;
    Delay(bme69x_get_meas_dur(BME69X_FORCED_MODE, &config_, &sensor_) + kHeaterMs * 1000, this);
    bme69x_data data[3]{};
    uint8_t count = 0;
    result = bme69x_get_data(BME69X_FORCED_MODE, data, &count, &sensor_);
    if (result != BME69X_OK || !count || !(data[0].status & BME69X_NEW_DATA_MSK))
        return false;
    const auto& value = data[0];
    const bool valid = std::isfinite(value.temperature) && std::isfinite(value.humidity) &&
                       std::isfinite(value.pressure) && value.temperature >= -40 &&
                       value.temperature <= 85 && value.humidity >= 0 && value.humidity <= 100 &&
                       value.pressure >= 30000 && value.pressure <= 110000;
    if (!valid)
        return false;
    Snapshot sample;
    {
        std::lock_guard lock(mutex_);
        snapshot_.sample_valid = true;
        snapshot_.sampled_at_us = esp_timer_get_time();
        snapshot_.temperature = value.temperature;
        snapshot_.humidity = value.humidity;
        snapshot_.pressure = value.pressure;
        snapshot_.gas_resistance = value.gas_resistance;
        snapshot_.gas_valid = (value.status & BME69X_GASM_VALID_MSK) &&
                              std::isfinite(value.gas_resistance) && value.gas_resistance > 0;
        snapshot_.heater_stable = value.status & BME69X_HEAT_STAB_MSK;
        ++snapshot_.samples;
        if (snapshot_.gas_valid && snapshot_.heater_stable) {
            ++snapshot_.valid_gas_samples;
            ++snapshot_.consecutive_valid_gas;
        } else {
            snapshot_.consecutive_valid_gas = 0;
        }
        sample = snapshot_;
    }
    if (sample.samples <= 5 || sample.samples % 5 == 0) {
        ESP_LOGI(TAG,
                 "Sample=%lu T=%.2fC RH=%.2f%% P=%.2fhPa gas=%.0fohm status=0x%02x "
                 "gas_valid=%d heater_stable=%d valid_gas=%lu errors=%lu stack_min_B=%u",
                 static_cast<unsigned long>(sample.samples), sample.temperature, sample.humidity,
                 sample.pressure / 100, sample.gas_resistance, value.status, sample.gas_valid,
                 sample.heater_stable, static_cast<unsigned long>(sample.valid_gas_samples),
                 static_cast<unsigned long>(sample.errors),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
    }
    return true;
}

void Bme690Nose::Disconnect() {
    if (device_) {
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
    }
    std::lock_guard lock(mutex_);
    snapshot_.detected = false;
    snapshot_.configured = false;
    snapshot_.sample_valid = false;
    snapshot_.gas_valid = false;
    snapshot_.heater_stable = false;
    snapshot_.consecutive_valid_gas = 0;
    // Never delete/recreate the shared I2C bus used by touch and BMI270.
}

void Bme690Nose::Run() {
    vTaskDelay(pdMS_TO_TICKS(1500));
    for (;;) {
        if (!Initialize()) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        uint32_t failures = 0;
        TickType_t wake = xTaskGetTickCount();
        for (;;) {
            if (Measure()) {
                failures = 0;
            } else {
                ++failures;
                {
                    std::lock_guard lock(mutex_);
                    ++snapshot_.errors;
                    snapshot_.sample_valid = false;
                    snapshot_.gas_valid = false;
                    snapshot_.heater_stable = false;
                    snapshot_.consecutive_valid_gas = 0;
                }
                ESP_LOGW(TAG, "No valid fresh measurement: consecutive=%lu/5",
                         static_cast<unsigned long>(failures));
                if (failures >= 5) {
                    Disconnect();
                    vTaskDelay(pdMS_TO_TICKS(10000));
                    break;
                }
            }
            if (xTaskGetTickCount() - wake >= pdMS_TO_TICKS(kPeriodMs))
                wake = xTaskGetTickCount();
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
        }
    }
}

cJSON* Bme690Nose::GetStatusJson() {
    Snapshot sample;
    {
        std::lock_guard lock(mutex_);
        sample = snapshot_;
    }
    const int64_t age_us = sample.sampled_at_us ? esp_timer_get_time() - sample.sampled_at_us : -1;
    const bool fresh = sample.sample_valid && age_us >= 0 && age_us <= kFreshUs;
    const bool usable = sample.configured && sample.self_test == BME69X_OK && fresh &&
                        sample.gas_valid && sample.heater_stable &&
                        sample.consecutive_valid_gas >= 3;
    cJSON* json = cJSON_CreateObject();
    if (!json)
        return nullptr;
    cJSON_AddStringToObject(json, "sensor", "BME690");
    cJSON_AddBoolToObject(json, "detected", sample.detected);
    cJSON_AddBoolToObject(json, "hardware_ok", usable);
    cJSON_AddBoolToObject(json, "configured", sample.configured);
    cJSON_AddBoolToObject(json, "fresh", fresh);
    cJSON_AddStringToObject(json, "self_test",
                            sample.self_test == 127         ? "not_run"
                            : sample.self_test == BME69X_OK ? "pass"
                                                            : "fail");
    cJSON_AddNumberToObject(json, "self_test_code", sample.self_test);
    cJSON_AddNumberToObject(json, "i2c_address", sample.address);
    cJSON_AddNumberToObject(json, "chip_id", sample.chip_id);
    cJSON_AddNumberToObject(json, "variant_id", sample.variant_id);
    cJSON_AddNumberToObject(json, "sample_age_ms", age_us < 0 ? -1 : age_us / 1000);
    cJSON_AddNumberToObject(json, "samples", sample.samples);
    cJSON_AddNumberToObject(json, "valid_gas_samples", sample.valid_gas_samples);
    cJSON_AddNumberToObject(json, "consecutive_valid_gas", sample.consecutive_valid_gas);
    cJSON_AddNumberToObject(json, "errors", sample.errors);
    cJSON_AddBoolToObject(json, "gas_valid", fresh && sample.gas_valid);
    cJSON_AddBoolToObject(json, "heater_stable", fresh && sample.heater_stable);
    if (fresh) {
        cJSON_AddNumberToObject(json, "temperature_c", sample.temperature);
        cJSON_AddNumberToObject(json, "relative_humidity_pct", sample.humidity);
        cJSON_AddNumberToObject(json, "pressure_hpa", sample.pressure / 100);
        if (sample.gas_valid && sample.heater_stable)
            cJSON_AddNumberToObject(json, "gas_resistance_ohm", sample.gas_resistance);
    }
    cJSON_AddBoolToObject(json, "iaq_available", false);
    cJSON_AddBoolToObject(json, "odor_classification_available", false);
    cJSON_AddStringToObject(
        json, "interpretation",
        "Raw gas response only. No BSEC IAQ or odor model; "
        "cannot identify a gas, quantify formaldehyde/CO2, or assess air safety.");
    // A registration log alone cannot prove the server called the tool.
    // Log a bounded health summary only when returning an actual query result.
    ESP_LOGI(TAG,
             "Query: hardware_ok=%d fresh=%d self_test=%d gas_valid=%d heater_stable=%d "
             "samples=%lu age_ms=%lld errors=%lu",
             usable, fresh, sample.self_test, fresh && sample.gas_valid,
             fresh && sample.heater_stable, static_cast<unsigned long>(sample.samples),
             static_cast<long long>(age_us < 0 ? -1 : age_us / 1000),
             static_cast<unsigned long>(sample.errors));
    return json;
}
