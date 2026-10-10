#include "pet_controller.h"

namespace {
constexpr PetPresentation kPresentations[] = {
    {PetAction::Idle, "neutral"},       {PetAction::Thinking, "thinking"},
    {PetAction::Listening, "thinking"}, {PetAction::Speaking, "happy"},
    {PetAction::Notifying, "winking"},  {PetAction::Setup, "thinking"},
    {PetAction::Surprise, "surprised"}, {PetAction::Celebrating, "happy"},
    {PetAction::Shaking, "surprised"},  {PetAction::TiltLeft, "curious"},
    {PetAction::TiltRight, "curious"},  {PetAction::UpsideDown, "shocked"},
    {PetAction::Sway, "neutral"},       {PetAction::Playing, "happy"},
    {PetAction::Sleeping, "sleepy"},
};
static_assert(sizeof(kPresentations) / sizeof(kPresentations[0]) ==
              static_cast<unsigned>(PetAction::Count));
bool IsTransient(PetAction action) {
    return action >= PetAction::Surprise && action <= PetAction::UpsideDown;
}
bool IsSetup(DeviceState state) {
    return state == kDeviceStateUnknown || state == kDeviceStateStarting ||
           state == kDeviceStateWifiConfiguring || state == kDeviceStateActivating ||
           state == kDeviceStateUpgrading;
}
PetAction ToAction(IdleAnimation animation) {
    switch (animation) {
        case IdleAnimation::Sway: return PetAction::Sway;
        case IdleAnimation::Play: return PetAction::Playing;
        case IdleAnimation::Sleep: return PetAction::Sleeping;
        default: return PetAction::Idle;
    }
}
uint32_t Duration(PetAction action) {
    return action == PetAction::Sway ? 3000 : 4000;
}
bool Equal(const char* text, const char* command) {
    while (*text && *text == *command) { ++text; ++command; }
    return *text == 0 && *command == 0;
}
bool StartsWith(const char* text, const char* command) {
    while (*command) { if (*text++ != *command++) return false; }
    return true;
}
// 固定缓冲，不复制任意长度的云端文本。UTF-8 中文标点按完整码点跳过。
// 返回 false 表示超长；仍可识别开头的“帮我”，但不能误判成完整短口令。
bool Normalize(const std::string& text, char (&out)[96]) {
    const char* input = text.c_str();
    size_t used = 0;
    bool complete = true;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        if (c <= 0x20 || (c >= 0x21 && c <= 0x2f) ||
            (c >= 0x3a && c <= 0x40) || (c >= 0x5b && c <= 0x60) || (c >= 0x7b && c <= 0x7e)) {
            ++i;
            continue;
        }
        size_t count = c < 0x80 ? 1 : (c < 0xe0 ? 2 : (c < 0xf0 ? 3 : 4));
        if (i + count > text.size()) { complete = false; break; }
        if (count == 3) {
            const unsigned char b = static_cast<unsigned char>(input[i + 1]);
            const unsigned char d = static_cast<unsigned char>(input[i + 2]);
            if ((c == 0xe3 && b == 0x80 && (d == 0x80 || d == 0x81 || d == 0x82 ||
                    (d >= 0x88 && d <= 0x91))) ||
                (c == 0xef && b == 0xbc && (d == 0x81 || d == 0x8c || d == 0x8e ||
                    d == 0x9a || d == 0x9b || d == 0x9f)) ||
                (c == 0xe2 && b == 0x80 && d >= 0x98 && d <= 0x9d)) {
                i += count;
                continue;
            }
        }
        if (used + count >= sizeof(out)) { complete = false; break; }
        for (size_t j = 0; j < count; ++j) out[used++] = input[i++];
    }
    out[used] = 0;
    return complete;
}
}  // namespace

void PetController::AdvanceSequence() {
    if (++presentation_sequence_ == 0) ++presentation_sequence_;
    awaiting_animation_ = false;
}
PetPresentation PetController::WithSequence(PetPresentation presentation) const {
    presentation.sequence = presentation_sequence_;
    return presentation;
}
void PetController::EnableAutonomy(uint32_t now_ms) {
    autonomy_enabled_ = true;
    greeting_pending_ = true;
    idle_policy_.Reset(now_ms);
}
void PetController::StartAction(PetAction action, uint32_t now_ms, uint32_t duration_ms) {
    transient_ = kPresentations[static_cast<uint8_t>(action)];
    transient_active_ = true;
    transient_deadline_ms_ = now_ms + duration_ms;
    idle_policy_.EnterIdle(now_ms);
    idle_animation_ = IdleAnimation::None;
    AdvanceSequence();
}
void PetController::StartAutonomy(uint32_t now_ms) {
    autonomy_running_ = true;
    idle_policy_.Reset(now_ms);
    idle_animation_ = IdleAnimation::None;
    AdvanceSequence();
    if (greeting_pending_) {
        greeting_pending_ = false;
        StartAction(PetAction::Surprise, now_ms, 4000);
    }
}
bool PetController::OnAssetsReady(uint32_t now_ms) {
    if (!autonomy_enabled_) return false;
    assets_ready_ = true;
    if (autonomy_running_ || IsSetup(state_)) return false;
    StartAutonomy(now_ms);
    return true;
}
PetPresentation PetController::OnDeviceState(DeviceState state, uint32_t now_ms) {
    const bool entering_setup = IsSetup(state) && !IsSetup(state_);
    state_ = state;
    if (state != kDeviceStateSpeaking) server_emotion_.clear();
    if (autonomy_enabled_) {
        // REVIEW[PET-AUDIO] 普通语音/连接状态只更新状态栏，不能清除动作或重置静音时间。
        if (IsSetup(state)) {
            base_ = kPresentations[static_cast<uint8_t>(PetAction::Setup)];
            if (autonomy_running_ || entering_setup) {
                autonomy_running_ = false;
                transient_active_ = false;
                idle_animation_ = IdleAnimation::None;
                idle_policy_.Reset(now_ms);
                AdvanceSequence();
            }
        } else {
            base_ = kPresentations[static_cast<uint8_t>(PetAction::Idle)];
            if (assets_ready_ && !autonomy_running_) StartAutonomy(now_ms);
        }
        return current();
    }
    // Other boards preserve their original service-state presentation.
    if (state != kDeviceStateIdle && state != kDeviceStateListening && state != kDeviceStateSpeaking)
        transient_active_ = false;
    switch (state) {
        case kDeviceStateConnecting: base_ = kPresentations[1]; break;
        case kDeviceStateListening: base_ = kPresentations[2]; break;
        case kDeviceStateSpeaking: base_ = kPresentations[3]; break;
        case kDeviceStateNotifying: base_ = kPresentations[4]; break;
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
        case kDeviceStateWifiConfiguring: base_ = kPresentations[5]; break;
        default: base_ = kPresentations[0]; break;
    }
    return current();
}
bool PetController::OnUserActivity(uint32_t now_ms) {
    if (!autonomy_running_ || IsSetup(state_)) return false;
    const auto before = idle_animation_;
    idle_policy_.RecordUserActivity(now_ms);
    idle_animation_ = idle_policy_.active();
    if (before != idle_animation_) { AdvanceSequence(); return true; }
    return false;
}
bool PetController::OnUserText(const std::string& text, uint32_t now_ms) {
    if (!autonomy_running_ || IsSetup(state_)) return false;
    char command[96];
    const bool complete = Normalize(text, command);
    if (command[0] == 0) return false;
    const bool changed = OnUserActivity(now_ms);
    // REVIEW[PET-COMMAND] 仅处理 STT 用户文本；不匹配助手回复或否定句中的子串。
    if (complete && (Equal(command, "睡觉") || Equal(command, "说出睡觉"))) {
        transient_active_ = false;
        idle_policy_.SleepNow();
        idle_animation_ = IdleAnimation::Sleep;
        AdvanceSequence();
        return true;
    }
    if (complete && Equal(command, "你好")) {
        StartAction(PetAction::Surprise, now_ms, 4000);
        return true;
    }
    if (StartsWith(command, "帮我")) {
        StartAction(PetAction::Thinking, now_ms, 6000);
        return true;
    }
    return changed;
}
bool PetController::SetTransientAction(PetAction action, uint32_t now_ms, uint32_t duration_ms) {
    if (!IsTransient(action) || duration_ms == 0 || duration_ms > INT32_MAX ||
        (!autonomy_enabled_ && !alert_emotion_.empty()) ||
        (autonomy_enabled_ ? !autonomy_running_ || IsSetup(state_) :
         state_ != kDeviceStateIdle && state_ != kDeviceStateListening && state_ != kDeviceStateSpeaking))
        return false;
    if (autonomy_enabled_) {
        // 触摸也是主动交互；可唤醒看山，但不改麦克风/音频服务状态。
        idle_policy_.RecordUserActivity(now_ms);
        StartAction(action, now_ms, duration_ms);
    } else {
        transient_ = kPresentations[static_cast<uint8_t>(action)];
        transient_active_ = true;
        transient_deadline_ms_ = now_ms + duration_ms;
    }
    return true;
}
bool PetController::CompleteAction(uint32_t now_ms) {
    if (transient_active_) {
        transient_active_ = false;
        idle_policy_.EnterIdle(now_ms);
        idle_animation_ = IdleAnimation::None;
    } else if (idle_animation_ == IdleAnimation::Sway || idle_animation_ == IdleAnimation::Play) {
        idle_animation_ = idle_policy_.CompleteAnimation();
    } else {
        return false;
    }
    if (idle_policy_.SilenceExpired(now_ms)) {
        idle_policy_.SleepNow();
        idle_animation_ = IdleAnimation::Sleep;
    }
    AdvanceSequence();
    action_deadline_ms_ = now_ms + Duration(ToAction(idle_animation_));
    return true;
}
bool PetController::OnAnimationEvent(uint32_t sequence, PetAnimationEvent event, uint32_t now_ms) {
    // REVIEW[PET-LIFETIME] 旧 GIF 完成事件可能已排队，序号不一致必须丢弃。
    if (!autonomy_running_ || sequence != presentation_sequence_ ||
        IsSetup(state_)) return false;
    if (!transient_active_ && idle_animation_ != IdleAnimation::Sway && idle_animation_ != IdleAnimation::Play)
        return false;
    if (event == PetAnimationEvent::Started) {
        awaiting_animation_ = true;
        action_deadline_ms_ = now_ms + 20000;  // 解码器挂起保护，正常切换由完成事件驱动。
        return false;
    }
    if (event == PetAnimationEvent::Failed) {
        // 保留静态回退图，最迟按素材时长退出；避免缺资源时在事件队列中高速切换。
        awaiting_animation_ = false;
        if (transient_active_) transient_deadline_ms_ = now_ms +
            (transient_.action == PetAction::Thinking ? 6000 : Duration(transient_.action));
        else action_deadline_ms_ = now_ms + Duration(ToAction(idle_animation_));
        return false;
    }
    return CompleteAction(now_ms);
}
bool PetController::Tick(uint32_t now_ms) {
    if (!autonomy_enabled_) {
        if (transient_active_ && static_cast<int32_t>(now_ms - transient_deadline_ms_) >= 0) {
            transient_active_ = false;
            return true;
        }
        return false;
    }
    if (!autonomy_running_ || IsSetup(state_)) return false;
    if (idle_policy_.SilenceExpired(now_ms) && idle_animation_ != IdleAnimation::Sleep) {
        transient_active_ = false;
        idle_policy_.SleepNow();
        idle_animation_ = IdleAnimation::Sleep;
        AdvanceSequence();
        return true;
    }
    if (transient_active_) {
        const uint32_t deadline = awaiting_animation_ ? action_deadline_ms_ : transient_deadline_ms_;
        return static_cast<int32_t>(now_ms - deadline) >= 0 && CompleteAction(now_ms);
    }
    if (idle_animation_ == IdleAnimation::Sway || idle_animation_ == IdleAnimation::Play)
        return static_cast<int32_t>(now_ms - action_deadline_ms_) >= 0 && CompleteAction(now_ms);
    const auto next = idle_policy_.Poll(now_ms);
    if (next == idle_animation_) return false;
    idle_animation_ = next;
    AdvanceSequence();
    action_deadline_ms_ = now_ms + Duration(ToAction(next));
    return true;
}
bool PetController::ClearMotionPose(uint32_t now_ms) {
    if (!transient_active_ || transient_.action < PetAction::TiltLeft || transient_.action > PetAction::UpsideDown)
        return false;
    transient_active_ = false;
    if (autonomy_enabled_) { idle_policy_.EnterIdle(now_ms); AdvanceSequence(); }
    return true;
}
bool PetController::SetServerEmotion(const std::string& emotion) {
    if (autonomy_enabled_) return false;  // 独立桌宠动作由本地事件决定。
    if (emotion.empty() || emotion.size() > 48 ||
        (state_ != kDeviceStateSpeaking && state_ != kDeviceStateListening && state_ != kDeviceStateConnecting))
        return false;
    for (char c : emotion) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    server_emotion_ = emotion;
    return true;
}
void PetController::SetAlertEmotion(const std::string& emotion) { alert_emotion_ = emotion; }
void PetController::ClearAlert() { alert_emotion_.clear(); }
PetPresentation PetController::current() const {
    if (autonomy_enabled_ && base_.action == PetAction::Setup) return WithSequence(base_);
    // 告警文字/提示音走原有 UI；只有配网、激活、升级等 Setup 状态覆盖桌宠。
    if (!autonomy_enabled_ && !alert_emotion_.empty())
        return {PetAction::Notifying, alert_emotion_.c_str()};
    if (transient_active_) return WithSequence(transient_);
    if (autonomy_enabled_) return WithSequence(kPresentations[static_cast<uint8_t>(ToAction(idle_animation_))]);
    if (state_ == kDeviceStateSpeaking && !server_emotion_.empty())
        return {PetAction::Speaking, server_emotion_.c_str()};
    return base_;
}
const char* PetController::source() const {
    if (autonomy_enabled_ && base_.action == PetAction::Setup) return "setup";
    if (!autonomy_enabled_ && !alert_emotion_.empty()) return "alert";
    if (transient_active_) return "interaction";
    if (autonomy_enabled_) return "autonomy";
    if (state_ == kDeviceStateSpeaking && !server_emotion_.empty()) return "server";
    return "state";
}
