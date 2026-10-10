#ifndef CYBER_KANSHAN_PET_CONTROLLER_H_
#define CYBER_KANSHAN_PET_CONTROLLER_H_

#include <cstdint>
#include <string>

#include "device_state.h"
#include "pet_idle_policy.h"

enum class PetAction : uint8_t {
    Idle,
    Thinking,
    Listening,
    Speaking,
    Notifying,
    Setup,
    Surprise,
    Celebrating,
    Shaking,
    TiltLeft,
    TiltRight,
    UpsideDown,
    Sway,
    Playing,
    Sleeping,
    Count,
};

struct PetPresentation {
    PetAction action;
    const char* emotion;
    uint32_t sequence = 0;  // 区分相同动作的重播，并拒绝旧播放器的完成事件。
};

enum class PetAnimationEvent : uint8_t { Started, Finished, Failed };

/** Maps XiaoZhi service states into the desk-pet presentation layer. */
class PetController {
public:
    PetPresentation OnDeviceState(DeviceState state, uint32_t now_ms = 0);
    void EnableAutonomy(uint32_t now_ms);
    bool OnAssetsReady(uint32_t now_ms);
    bool OnUserActivity(uint32_t now_ms);
    bool OnUserText(const std::string& text, uint32_t now_ms);
    bool OnAnimationEvent(uint32_t sequence, PetAnimationEvent event, uint32_t now_ms);
    bool SetTransientAction(PetAction action, uint32_t now_ms, uint32_t duration_ms);
    bool ClearMotionPose(uint32_t now_ms = 0);
    bool SetServerEmotion(const std::string& emotion);
    void SetAlertEmotion(const std::string& emotion);
    void ClearAlert();
    bool Tick(uint32_t now_ms);
    PetPresentation current() const;
    const char* source() const;

private:
    PetPresentation base_{PetAction::Idle, "neutral"};
    PetPresentation transient_{PetAction::Surprise, "surprised"};
    DeviceState state_ = kDeviceStateIdle;
    std::string server_emotion_;
    std::string alert_emotion_;
    bool transient_active_ = false;
    uint32_t transient_deadline_ms_ = 0;
    bool autonomy_enabled_ = false;
    bool greeting_pending_ = false;
    bool assets_ready_ = false;
    bool autonomy_running_ = false;
    bool awaiting_animation_ = false;
    uint32_t presentation_sequence_ = 0;
    uint32_t action_deadline_ms_ = 0;
    PetIdlePolicy idle_policy_;
    IdleAnimation idle_animation_ = IdleAnimation::None;
    void StartAutonomy(uint32_t now_ms);
    void StartAction(PetAction action, uint32_t now_ms, uint32_t duration_ms);
    bool CompleteAction(uint32_t now_ms);
    void AdvanceSequence();
    PetPresentation WithSequence(PetPresentation presentation) const;
};

#endif  // CYBER_KANSHAN_PET_CONTROLLER_H_
