#pragma once

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "bmi270_api.h"
#include "pet/motion_detector.h"

// Board-lifetime service; owns only the BMI270 device, never the shared touch bus.
class Bmi270PetMotion {
public:
    void Start(i2c_master_bus_handle_t native_bus);

private:
    void Run();
    bool Initialize();
    i2c_master_bus_handle_t native_bus_ = nullptr;
    i2c_bus_handle_t bus_ = nullptr;
    bmi270_handle_t sensor_ = nullptr;
    TaskHandle_t task_ = nullptr;
    MotionDetector detector_;
};
