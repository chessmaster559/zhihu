#pragma once

#include <stdint.h>

enum class PetMotion : uint8_t { None, Shake, TiltLeft, TiltRight, UpsideDown, Level };

struct MotionSample {
    float ax, ay, az;  // g, including gravity
    float gx, gy, gz;  // degrees/second
};

// No RTOS, LVGL or dynamic allocation: independently testable motion policy.
class MotionDetector {
public:
    PetMotion Update(const MotionSample& sample, uint32_t now_ms);
    void Reset();
    bool calibrated() const { return calibrated_; }
    float lateral() const { return lateral_; }
    float upright() const { return upright_; }
    static const char* Name(PetMotion motion);

private:
    MotionSample gravity_{};
    float reference_[3]{};
    float horizontal_[3]{};
    float calibration_sum_[3]{};
    uint16_t calibration_count_ = 0;
    bool calibrated_ = false;
    bool have_sample_ = false;
    uint32_t last_sample_ms_ = 0;
    uint32_t candidate_since_ms_ = 0;
    uint32_t last_pose_event_ms_ = 0;
    uint32_t peak_window_ms_ = 0;
    uint32_t last_peak_ms_ = 0;
    uint32_t last_shake_ms_ = 0;
    uint8_t peaks_ = 0;
    bool above_peak_ = false;
    bool shake_cooldown_ = false;
    PetMotion candidate_ = PetMotion::Level;
    PetMotion pose_ = PetMotion::Level;
    float lateral_ = 0;
    float upright_ = 1;
};
