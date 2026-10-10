#include "motion_detector.h"

namespace {
float Length(float x, float y, float z) {
    const float squared = x * x + y * y + z * z;
    if (squared < 0.000001f)
        return 0;
    // Newton iteration avoids a platform-specific math dependency in host tests.
    float result = squared > 1 ? squared : 1;
    for (int i = 0; i < 12; ++i)
        result = 0.5f * (result + squared / result);
    return result;
}
float Square(float value) { return value * value; }
}  // namespace

void MotionDetector::Reset() { *this = MotionDetector{}; }

PetMotion MotionDetector::Update(const MotionSample& s, uint32_t now) {
    // REVIEW[KEY-CALIBRATION] 基线来自约 50 个稳定样本；检测相对启动姿态，不代表绝对屏幕朝向。
    // 无效数据或 >200ms 间隔会丢弃校准；恢复采样后需要重新静置。
    // Invalid readings never become UI events. A sampling gap requires recalibration.
    if (!(s.ax >= -4 && s.ax <= 4 && s.ay >= -4 && s.ay <= 4 && s.az >= -4 && s.az <= 4 &&
          s.gx >= -500 && s.gx <= 500 && s.gy >= -500 && s.gy <= 500 && s.gz >= -500 &&
          s.gz <= 500)) {
        Reset();
        return PetMotion::None;
    }
    if (have_sample_ && now - last_sample_ms_ > 200)
        Reset();
    if (!have_sample_)
        gravity_ = s;
    have_sample_ = true;
    last_sample_ms_ = now;

    if (!calibrated_) {
        const float magnitude = Length(s.ax, s.ay, s.az);
        const float motion =
            Square(s.ax - gravity_.ax) + Square(s.ay - gravity_.ay) + Square(s.az - gravity_.az);
        const bool stable = magnitude > 0.85f && magnitude < 1.15f && motion < 0.015f &&
                            Square(s.gx) + Square(s.gy) + Square(s.gz) < 225;
        gravity_ = s;
        if (!stable) {
            calibration_count_ = 0;
            for (auto& value : calibration_sum_)
                value = 0;
            return PetMotion::None;
        }
        calibration_sum_[0] += s.ax;
        calibration_sum_[1] += s.ay;
        calibration_sum_[2] += s.az;
        if (++calibration_count_ < 50)
            return PetMotion::None;
        const float norm = Length(calibration_sum_[0], calibration_sum_[1], calibration_sum_[2]);
        for (int i = 0; i < 3; ++i)
            reference_[i] = calibration_sum_[i] / norm;
        // Project sensor X onto the baseline plane; use Y when X is almost vertical.
        const int axis = reference_[0] * reference_[0] < 0.8f ? 0 : 1;
        for (int i = 0; i < 3; ++i) {
            horizontal_[i] = (i == axis ? 1.0f : 0.0f) - reference_[axis] * reference_[i];
        }
        const float hn = Length(horizontal_[0], horizontal_[1], horizontal_[2]);
        for (auto& value : horizontal_)
            value /= hn;
        calibrated_ = true;
        candidate_since_ms_ = last_pose_event_ms_ = now;
        return PetMotion::None;
    }

    // 50 Hz -> about 100 ms gravity low-pass; shake uses the removed high-pass part.
    gravity_.ax += 0.18f * (s.ax - gravity_.ax);
    gravity_.ay += 0.18f * (s.ay - gravity_.ay);
    gravity_.az += 0.18f * (s.az - gravity_.az);
    const float dynamic2 =
        Square(s.ax - gravity_.ax) + Square(s.ay - gravity_.ay) + Square(s.az - gravity_.az);
    const bool peak = dynamic2 > 0.64f;
    if (!peak)
        above_peak_ = false;
    if (shake_cooldown_ && now - last_shake_ms_ >= 1500)
        shake_cooldown_ = false;
    if (!shake_cooldown_ && peak && !above_peak_ && now - last_peak_ms_ >= 60) {
        above_peak_ = true;
        last_peak_ms_ = now;
        if (peaks_ == 0 || now - peak_window_ms_ > 600) {
            peaks_ = 0;
            peak_window_ms_ = now;
        }
        if (++peaks_ >= 3) {
            peaks_ = 0;
            shake_cooldown_ = true;
            last_shake_ms_ = now;
            candidate_since_ms_ = now;
            return PetMotion::Shake;
        }
    }
    const float norm = Length(gravity_.ax, gravity_.ay, gravity_.az);
    if (norm < 0.5f || dynamic2 > 0.16f || Square(s.gx) + Square(s.gy) + Square(s.gz) > 3600) {
        candidate_since_ms_ = now;
        return PetMotion::None;
    }
    upright_ =
        (gravity_.ax * reference_[0] + gravity_.ay * reference_[1] + gravity_.az * reference_[2]) /
        norm;
    lateral_ = (gravity_.ax * horizontal_[0] + gravity_.ay * horizontal_[1] +
                gravity_.az * horizontal_[2]) /
               norm;
    PetMotion next = PetMotion::Level;
    if (upright_ < (pose_ == PetMotion::UpsideDown ? -0.45f : -0.65f)) {
        next = PetMotion::UpsideDown;
    } else if (lateral_ > (pose_ == PetMotion::TiltRight ? 0.28f : 0.45f)) {
        next = PetMotion::TiltRight;
    } else if (lateral_ < (pose_ == PetMotion::TiltLeft ? -0.28f : -0.45f)) {
        next = PetMotion::TiltLeft;
    }
    if (next != candidate_) {
        candidate_ = next;
        candidate_since_ms_ = now;
    }
    if (now - candidate_since_ms_ < 250 || shake_cooldown_)
        // REVIEW[KEY-DEBOUNCE] 250ms 姿态防抖和摇晃冷却共同抑制事件，保持姿态最多每秒续期一次。
        return PetMotion::None;
    // Held poses renew at <= 1 Hz, not every sensor frame. Returning level clears a pose.
    if (next != pose_ || (next != PetMotion::Level && now - last_pose_event_ms_ >= 1000)) {
        pose_ = next;
        last_pose_event_ms_ = now;
        return next;
    }
    return PetMotion::None;
}

const char* MotionDetector::Name(PetMotion motion) {
    switch (motion) {
        case PetMotion::Shake:
            return "shake";
        case PetMotion::TiltLeft:
            return "tilt_left";
        case PetMotion::TiltRight:
            return "tilt_right";
        case PetMotion::UpsideDown:
            return "upside_down";
        case PetMotion::Level:
            return "level";
        default:
            return "none";
    }
}
