#include "bmi270_pet_motion.h"

#include <esp_log.h>
#include <esp_timer.h>
#include "application.h"
#include "config.h"

namespace {
constexpr char TAG[] = "Bmi270Pet";
constexpr uint32_t kPeriodMs = 20;
}  // namespace

void Bmi270PetMotion::Start(i2c_master_bus_handle_t native_bus) {
    if (task_ || !native_bus)
        return;
    // REVIEW[FIX-IMU-CS] 先预置输出锁存值，再使能输出，避免 CS 先低后高。
    // BMI270 上电后的 CS 上升沿会锁定 SPI 模式，I2C 将无应答（手册 6.3）。
    // 固件只能避免自身制造的跳变；已经切到 SPI 的芯片需要复位/重新上电。
    esp_err_t err = gpio_set_level(BMI270_CS_GPIO, 1);
    if (err == ESP_OK)
        err = gpio_set_level(BMI270_SDO_GPIO, 0);
    gpio_config_t pins{};
    pins.pin_bit_mask = (1ULL << BMI270_CS_GPIO) | (1ULL << BMI270_SDO_GPIO);
    pins.mode = GPIO_MODE_OUTPUT;
    if (err == ESP_OK)
        err = gpio_config(&pins);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pin configuration failed: %s", esp_err_to_name(err));
        return;
    }
    native_bus_ = native_bus;
    if (xTaskCreate([](void* arg) { static_cast<Bmi270PetMotion*>(arg)->Run(); }, "bmi270_pet",
                    4096, this, 2, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "Task allocation failed: stack=4096B priority=2; voice/display unaffected");
    }
}

bool Bmi270PetMotion::Initialize() {
    esp_err_t err = i2c_master_probe(native_bus_, BMI270_PET_I2C_ADDRESS, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Probe 0x68 failed: %s; check BMI270 daughterboard, 3.3V, SDA=2 SCL=3 "
                 "CS=10 high SDO=9 low. Retry in 10s",
                 esp_err_to_name(err));
        return false;
    }
    if (!bus_) {
        // REVIEW[KEY-IMU] 探测失败首先表示地址无应答；不能仅靠动画播放认定传感器采集成功。
        i2c_config_t conf{};
        conf.mode = I2C_MODE_MASTER;
        conf.sda_io_num = LCD_TP_SDA;
        conf.scl_io_num = LCD_TP_SCL;
        conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        conf.master.clk_speed = 400000;
        // i2c_bus v2 adopts the already-created IDF master bus. Never delete this wrapper:
        // its delete API would also remove the native bus used by the touch controller.
        bus_ = i2c_bus_create(I2C_NUM_0, &conf);
    }
    if (!bus_)
        return false;
    err = bmi270_sensor_create(bus_, &sensor_, bmi270_config_file,
                               BMI2_GYRO_CROSS_SENS_ENABLE | BMI2_CRT_RTOSK_ENABLE);
    if (err != ESP_OK || !sensor_) {
        ESP_LOGE(TAG, "Sensor create failed: %s", esp_err_to_name(err));
        if (sensor_)
            bmi270_sensor_del(&sensor_);
        return false;
    }
    int8_t result = bmi2_set_adv_power_save(BMI2_DISABLE, sensor_);
    bmi2_sens_config configs[2]{};
    configs[0].type = BMI2_ACCEL;
    configs[1].type = BMI2_GYRO;
    if (result == BMI2_OK)
        result = bmi270_get_sensor_config(configs, 2, sensor_);
    if (result == BMI2_OK) {
        configs[0].cfg.acc.odr = BMI2_ACC_ODR_50HZ;
        configs[0].cfg.acc.range = BMI2_ACC_RANGE_4G;
        configs[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
        configs[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
        configs[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
        configs[1].cfg.gyr.range = BMI2_GYR_RANGE_500;
        configs[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
        configs[1].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
        configs[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
        result = bmi270_set_sensor_config(configs, 2, sensor_);
    }
    const uint8_t sensors[] = {BMI2_ACCEL, BMI2_GYRO};
    if (result == BMI2_OK)
        result = bmi270_sensor_enable(sensors, 2, sensor_);
    if (result != BMI2_OK || sensor_->chip_id != BMI270_CHIP_ID) {
        ESP_LOGE(TAG, "Configuration failed: result=%d chip_id=0x%02x", result, sensor_->chip_id);
        bmi270_sensor_del(&sensor_);
        return false;
    }
    detector_.Reset();
    ESP_LOGI(TAG,
             "Ready: chip=0x%02x addr=0x68 shared_i2c=400kHz acc=50Hz/+/-4g "
             "gyro=100Hz/+/-500dps poll=50Hz stack=4096B. Keep still ~1s to calibrate; "
             "INT1/BOOT=28 and INT2=0 unused",
             sensor_->chip_id);
    ESP_LOGI(TAG, "BMM350 0x14: %s (compass not enabled)",
             i2c_master_probe(native_bus_, 0x14, 100) == ESP_OK ? "present" : "not detected");
    return true;
}

void Bmi270PetMotion::Run() {
    // Let the board constructor and application setup finish before posting any events.
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t total_errors = 0;
    for (;;) {
        if (!Initialize()) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Gyro startup settling.
        TickType_t wake = xTaskGetTickCount();
        uint32_t stats_ms = esp_timer_get_time() / 1000;
        uint32_t count = 0, consecutive_errors = 0, max_read_us = 0;
        bool was_calibrated = false;
        MotionSample sample{};
        for (;;) {
            bmi2_sens_data data{};
            const int64_t start = esp_timer_get_time();
            const int8_t result = bmi2_get_sensor_data(&data, sensor_);
            const uint32_t read_us = esp_timer_get_time() - start;
            if (read_us > max_read_us)
                max_read_us = read_us;
            const uint32_t now = esp_timer_get_time() / 1000;
            if (result != BMI2_OK) {
                ++total_errors;
                if (++consecutive_errors == 1) {
                    ESP_LOGW(TAG, "Read failed: result=%d total_errors=%lu", result,
                             static_cast<unsigned long>(total_errors));
                }
                if (consecutive_errors >= 10) {
                    ESP_LOGE(TAG, "10 consecutive read failures; reinitialize in 10s");
                    Application::GetInstance().NotifyPetMotion(PetMotion::Level);
                    bmi270_sensor_del(&sensor_);
                    vTaskDelay(pdMS_TO_TICKS(10000));
                    break;
                }
            } else {
                if (consecutive_errors)
                    ESP_LOGI(TAG, "Read recovered");
                consecutive_errors = 0;
                ++count;
                // REVIEW[KEY-UNITS] 比例对应上面的 +/-4g、+/-500dps；更改量程需同步换算。
                sample = {data.acc.x / 8192.0f, data.acc.y / 8192.0f, data.acc.z / 8192.0f,
                          data.gyr.x / 65.536f, data.gyr.y / 65.536f, data.gyr.z / 65.536f};
                auto event = detector_.Update(sample, now);
#if BMI270_PET_LATERAL_SIGN < 0
                if (event == PetMotion::TiltLeft)
                    event = PetMotion::TiltRight;
                else if (event == PetMotion::TiltRight)
                    event = PetMotion::TiltLeft;
#endif
                if (detector_.calibrated() != was_calibrated) {
                    was_calibrated = detector_.calibrated();
                    ESP_LOGI(TAG, "Calibration: %s acc_g=(%.3f,%.3f,%.3f)",
                             was_calibrated ? "ready" : "reset; keep still", sample.ax, sample.ay,
                             sample.az);
                }
                if (event != PetMotion::None) {
                    ESP_LOGI(TAG, "Motion=%s lateral=%.2f upright=%.2f",
                             MotionDetector::Name(event), detector_.lateral(), detector_.upright());
                    Application::GetInstance().NotifyPetMotion(event);
                    // REVIEW[KEY-THREAD] 本任务只投递动作；PetController 和 LVGL
                    // 由应用/显示任务处理。
                }
            }
            if (now - stats_ms >= 5000) {
                ESP_LOGI(TAG,
                         "Stats: hz=%.1f calibrated=%d acc_g=(%.2f,%.2f,%.2f) "
                         "gyro_dps=(%.1f,%.1f,%.1f) read_max_us=%lu errors=%lu stack_min_B=%u",
                         count * 1000.0f / (now - stats_ms), detector_.calibrated(), sample.ax,
                         sample.ay, sample.az, sample.gx, sample.gy, sample.gz,
                         static_cast<unsigned long>(max_read_us),
                         static_cast<unsigned long>(total_errors),
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                                               sizeof(StackType_t)));
                count = max_read_us = 0;
                stats_ms = now;
            }
            // Avoid catch-up bursts following an I2C timeout.
            if (xTaskGetTickCount() - wake >= pdMS_TO_TICKS(kPeriodMs))
                wake = xTaskGetTickCount();
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
        }
    }
}
