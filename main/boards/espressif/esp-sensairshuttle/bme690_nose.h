#pragma once

#include <driver/i2c_master.h>
#include <bme69x.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

// Board-lifetime service. Only its worker accesses the sensor; MCP reads a
// bounded cached snapshot, never waiting for a heater cycle or touching LVGL.
class Bme690Nose {
public:
    void Start(i2c_master_bus_handle_t bus);
    cJSON* GetStatusJson();  // Transfers ownership to the MCP caller.

private:
    struct Snapshot {
        bool detected = false;
        bool configured = false;
        bool sample_valid = false;
        bool gas_valid = false;
        bool heater_stable = false;
        int self_test = 127;  // Not attempted; Bosch success is zero.
        uint8_t address = 0;
        uint8_t chip_id = 0;
        uint32_t variant_id = 0;
        uint32_t samples = 0;
        uint32_t valid_gas_samples = 0;
        uint32_t consecutive_valid_gas = 0;
        uint32_t errors = 0;
        int64_t sampled_at_us = 0;
        float temperature = 0;
        float humidity = 0;
        float pressure = 0;
        float gas_resistance = 0;
    };
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bme69x_dev sensor_{};
    bme69x_conf config_{};
    std::mutex mutex_;
    Snapshot snapshot_;
    bool self_test_attempted_ = false;
    void Run();
    bool Initialize();
    bool Measure();
    void Disconnect();
    static BME69X_INTF_RET_TYPE Read(uint8_t reg, uint8_t* data, uint32_t len, void* context);
    static BME69X_INTF_RET_TYPE Write(uint8_t reg, const uint8_t* data, uint32_t len,
                                      void* context);
    static void Delay(uint32_t period_us, void* context);
};
