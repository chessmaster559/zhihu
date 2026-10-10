#pragma once
#include <stdint.h>

enum class IdleAnimation : uint8_t { None, Sway, Play, Sleep };

// 只由应用主任务调用。用户输入计时与待机计时分离，播报不刷新二者。
class PetIdlePolicy {
public:
    void Reset(uint32_t now_ms) {
        last_input_ms_ = now_ms;
        EnterIdle(now_ms);
    }
    void EnterIdle(uint32_t now_ms) {
        idle_since_ms_ = now_ms;
        active_ = IdleAnimation::None;
    }
    void RecordUserActivity(uint32_t now_ms) {
        last_input_ms_ = now_ms;
        // 普通输入保留正在进行的自主动作；瞌睡被用户输入唤醒。
        if (active_ == IdleAnimation::Sleep || active_ == IdleAnimation::None)
            EnterIdle(now_ms);
    }
    void SleepNow() { active_ = IdleAnimation::Sleep; }
    bool SilenceExpired(uint32_t now_ms) const {
        return now_ms - last_input_ms_ >= 30000;
    }
    IdleAnimation Poll(uint32_t now_ms) {
        // REVIEW[PET-SILENCE] 动画完成、TTS 和 listening 状态都不能刷新静音时间。
        if (SilenceExpired(now_ms))
            active_ = IdleAnimation::Sleep;
        else if (active_ == IdleAnimation::None && now_ms - idle_since_ms_ >= 10000)
            active_ = IdleAnimation::Sway;
        return active_;
    }
    IdleAnimation CompleteAnimation() {
        if (active_ == IdleAnimation::Sway)
            active_ = IdleAnimation::Play;
        else if (active_ == IdleAnimation::Play)
            active_ = IdleAnimation::Sway;
        return active_;
    }
    IdleAnimation active() const { return active_; }
private:
    uint32_t last_input_ms_ = 0, idle_since_ms_ = 0;
    IdleAnimation active_ = IdleAnimation::None;
};
