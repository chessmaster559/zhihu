#include "pet_animation.h"
#define CHECK(condition)     \
    do {                     \
        if (!(condition))    \
            return __LINE__; \
    } while (0)
extern "C" void* memset(void* p, int c, size_t n) {
    auto b = static_cast<unsigned char*>(p);
    for (size_t i = 0; i < n; ++i)
        b[i] = static_cast<unsigned char>(c);
    return p;
}
extern "C" void* memcpy(void* p, const void* q, size_t n) {
    auto b = static_cast<unsigned char*>(p);
    auto a = static_cast<const unsigned char*>(q);
    for (size_t i = 0; i < n; ++i)
        b[i] = a[i];
    return p;
}
bool SameText(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
int main() {
    CHECK(GetPetAnimationSpec(PetAction::Setup).portrait);
    CHECK(GetPetAnimationSpec(PetAction::Surprise).single);
    CHECK(SameText(GetPetAnimationSpec(PetAction::Surprise).asset, "pet_greeting"));
    CHECK(SameText(GetPetAnimationSpec(PetAction::Thinking).asset, "pet_thinking"));
    CHECK(SameText(GetPetAnimationSpec(PetAction::Sway).asset, "pet_sway"));
    CHECK(SameText(GetPetAnimationSpec(PetAction::Playing).asset, "pet_play"));
    CHECK(SameText(GetPetAnimationSpec(PetAction::Sleeping).asset, "pet_sleep"));
    CHECK(!GetPetAnimationSpec(PetAction::Sleeping).single);
    PetController pet;
    pet.EnableAutonomy(0);
    CHECK(pet.OnDeviceState(kDeviceStateStarting, 0).action == PetAction::Setup);
    pet.SetAlertEmotion("gear");
    CHECK(pet.current().action == PetAction::Setup);
    CHECK(pet.OnDeviceState(kDeviceStateWifiConfiguring, 1000).action == PetAction::Setup);
    CHECK(!pet.OnAssetsReady(1000));  // Assets alone cannot replace provisioning portrait.
    CHECK(!pet.SetTransientAction(PetAction::Surprise, 1000, 4000));
    CHECK(!pet.OnUserText("你好", 1000));
    pet.ClearAlert();
    CHECK(pet.OnDeviceState(kDeviceStateIdle, 2000).action == PetAction::Surprise);
    const auto boot = pet.current().sequence;
    CHECK(!pet.OnAnimationEvent(boot, PetAnimationEvent::Started, 2000));
    CHECK(pet.OnDeviceState(kDeviceStateSpeaking, 2500).action == PetAction::Surprise);
    CHECK(!pet.SetServerEmotion("happy"));
    CHECK(!pet.Tick(6000));  // Wait for real end, not nominal duration under load.
    CHECK(pet.OnAnimationEvent(boot, PetAnimationEvent::Finished, 6500));
    CHECK(pet.current().action == PetAction::Idle);
    CHECK(!pet.OnAnimationEvent(boot, PetAnimationEvent::Finished, 7000));
    CHECK(!pet.Tick(16499));
    CHECK(pet.Tick(16500));
    CHECK(pet.current().action == PetAction::Sway);
    const auto sway = pet.current().sequence;
    CHECK(!pet.OnAnimationEvent(sway, PetAnimationEvent::Started, 16500));
    CHECK(pet.OnDeviceState(kDeviceStateListening, 17000).action == PetAction::Sway);
    CHECK(pet.OnDeviceState(kDeviceStateSpeaking, 18000).action == PetAction::Sway);
    pet.SetAlertEmotion("gear");
    CHECK(pet.current().action == PetAction::Sway);  // Spoken alerts keep the action too.
    CHECK(pet.OnDeviceState(kDeviceStateNotifying, 19000).action == PetAction::Sway);
    CHECK(pet.current().sequence == sway);
    CHECK(pet.OnAnimationEvent(sway, PetAnimationEvent::Finished, 20000));
    CHECK(pet.current().action == PetAction::Playing);
    pet.ClearAlert();
    CHECK(!pet.OnAnimationEvent(pet.current().sequence, PetAnimationEvent::Started, 20000));
    CHECK(pet.OnAnimationEvent(pet.current().sequence, PetAnimationEvent::Finished, 24000));
    CHECK(pet.current().action == PetAction::Sway);
    CHECK(!pet.OnAnimationEvent(pet.current().sequence, PetAnimationEvent::Started, 24000));
    CHECK(!pet.Tick(31999));  // No input since normal startup at 2000.
    CHECK(pet.Tick(32000));
    CHECK(pet.current().action == PetAction::Sleeping);
    CHECK(pet.OnDeviceState(kDeviceStateSpeaking, 33000).action == PetAction::Sleeping);
    CHECK(!pet.Tick(34000));
    CHECK(pet.OnUserText("帮我查一下天气。", 40000));
    CHECK(pet.current().action == PetAction::Thinking);
    const auto computer = pet.current().sequence;
    CHECK(!pet.OnAnimationEvent(computer, PetAnimationEvent::Started, 40000));
    CHECK(pet.OnDeviceState(kDeviceStateConnecting, 40100).action == PetAction::Thinking);
    CHECK(pet.OnDeviceState(kDeviceStateSpeaking, 41000).action == PetAction::Thinking);
    CHECK(!pet.Tick(46000));
    CHECK(pet.OnAnimationEvent(computer, PetAnimationEvent::Finished, 47000));
    CHECK(pet.current().action == PetAction::Idle);
    CHECK(!pet.Tick(56999));
    CHECK(pet.Tick(57000));
    CHECK(pet.current().action == PetAction::Sway);
    CHECK(!pet.OnUserText("今天天气怎么样", 58000));
    CHECK(pet.current().action == PetAction::Sway);
    CHECK(!pet.Tick(58000));
    CHECK(pet.OnUserText(" 你 好！ ", 60000));
    const auto greeting = pet.current().sequence;
    CHECK(pet.current().action == PetAction::Surprise);
    CHECK(pet.OnUserText("你好。", 60100));  // Same animation is explicitly replayed.
    CHECK(pet.current().sequence != greeting);
    CHECK(!pet.OnAnimationEvent(greeting, PetAnimationEvent::Finished, 60200));
    CHECK(!pet.OnAnimationEvent(greeting, PetAnimationEvent::Started, 60200));
    CHECK(pet.OnUserText("睡觉！", 61000));
    CHECK(pet.current().action == PetAction::Sleeping);
    CHECK(pet.OnDeviceState(kDeviceStateListening, 62000).action == PetAction::Sleeping);
    CHECK(!pet.Tick(62000));
    CHECK(!pet.OnUserText(" ！？ ", 62001));  // Punctuation alone is not activity.
    CHECK(pet.current().action == PetAction::Sleeping);
    CHECK(pet.OnUserText("不要睡觉", 63000));  // Wakes, but does not execute substring.
    CHECK(pet.current().action == PetAction::Idle);
    CHECK(!pet.OnUserText("他跟我说你好", 64000));
    CHECK(pet.current().action == PetAction::Idle);
    CHECK(pet.OnUserText("说出睡觉", 65000));
    CHECK(pet.OnUserActivity(66000));  // Wake word also wakes pet.
    CHECK(pet.current().action == PetAction::Idle);
    CHECK(!pet.Tick(75999));
    CHECK(pet.Tick(76000));
    CHECK(pet.current().action == PetAction::Sway);
    CHECK(pet.OnUserText("帮我", 80000));
    CHECK(!pet.OnAnimationEvent(pet.current().sequence, PetAnimationEvent::Failed, 80000));
    CHECK(!pet.Tick(85999));
    CHECK(pet.Tick(86000));  // Missing asset still exits after clip duration.
    CHECK(pet.current().action == PetAction::Idle);
    CHECK(pet.OnUserText("你好", 90000));
    CHECK(!pet.OnAnimationEvent(pet.current().sequence, PetAnimationEvent::Started, 90000));
    CHECK(!pet.Tick(109999));
    CHECK(pet.Tick(110000));  // Decoder watchdog; no leaked permanent command.
    CHECK(pet.current().action == PetAction::Idle);
    CHECK(pet.OnDeviceState(kDeviceStateWifiConfiguring, 110100).action == PetAction::Setup);
    CHECK(!pet.Tick(130000));
    CHECK(pet.OnDeviceState(kDeviceStateIdle, 140000).action ==
          PetAction::Idle);  // Greeting once per boot.
    CHECK(!pet.Tick(149999));
    CHECK(pet.Tick(150000));
    CHECK(pet.OnDeviceState(kDeviceStateUpgrading, 151000).action == PetAction::Setup);
    CHECK(!pet.OnAnimationEvent(pet.current().sequence, PetAnimationEvent::Finished, 151001));

    PetController delayed;
    delayed.EnableAutonomy(0);
    delayed.OnDeviceState(kDeviceStateIdle, 1000);
    CHECK(!delayed.Tick(90000));  // Do not consume greeting before assets arrive.
    CHECK(delayed.OnAssetsReady(90000));
    CHECK(delayed.current().action == PetAction::Surprise);
    CHECK(delayed.Tick(94000));  // Fallback when display supplies no playback events.
    CHECK(delayed.current().action == PetAction::Idle);
    CHECK(!delayed.OnAssetsReady(95000));
    CHECK(delayed.OnUserText("你好", 100000));
    CHECK(
        !delayed.OnAnimationEvent(delayed.current().sequence, PetAnimationEvent::Started, 100000));
    CHECK(delayed.OnUserText("普通语音", 101000) == false);
    CHECK(delayed.current().action == PetAction::Surprise);
    CHECK(!delayed.OnUserText("", 102000));
    CHECK(
        delayed.OnAnimationEvent(delayed.current().sequence, PetAnimationEvent::Finished, 105000));
    CHECK(delayed.Tick(131000));
    CHECK(delayed.current().action == PetAction::Sleeping);
    CHECK(delayed.OnUserText("‘你好’。", 132000));
    CHECK(delayed.current().action == PetAction::Surprise);
    CHECK(delayed.Tick(136000));
    char long_text[120];
    const char* prefix = "帮我";
    for (size_t i = 0; i < 6; ++i)
        long_text[i] = prefix[i];
    for (size_t i = 6; i < sizeof(long_text) - 1; ++i)
        long_text[i] = 'a';
    long_text[sizeof(long_text) - 1] = 0;
    CHECK(delayed.OnUserText(long_text, 137000));  // Bounded parser accepts long help prefix.
    CHECK(delayed.current().action == PetAction::Thinking);
    CHECK(delayed.Tick(143000));
    prefix = "睡觉";
    for (size_t i = 0; i < 6; ++i)
        long_text[i] = prefix[i];
    CHECK(!delayed.OnUserText(long_text, 144000));
    CHECK(delayed.current().action == PetAction::Idle);  // Truncation is never an exact command.

    PetController wrapped;
    const uint32_t start = UINT32_MAX - 5000;
    wrapped.EnableAutonomy(start);
    wrapped.OnAssetsReady(start);
    wrapped.OnDeviceState(kDeviceStateIdle, start);
    CHECK(wrapped.Tick(start + 4000));
    CHECK(!wrapped.Tick(start + 13999));
    CHECK(wrapped.Tick(start + 14000));
    CHECK(wrapped.current().action == PetAction::Sway);
    CHECK(wrapped.Tick(start + 30000));
    CHECK(wrapped.current().action == PetAction::Sleeping);

    PetController generic;
    CHECK(generic.OnDeviceState(kDeviceStateStarting).action == PetAction::Idle);
    CHECK(!generic.OnAssetsReady(0));
    CHECK(!generic.OnUserText("你好", 1));
    CHECK(generic.OnDeviceState(kDeviceStateListening).action == PetAction::Listening);
    CHECK(generic.SetServerEmotion("happy"));
    CHECK(!generic.SetServerEmotion("not valid"));
    CHECK(generic.OnDeviceState(kDeviceStateSpeaking).action == PetAction::Speaking);
    CHECK(generic.SetTransientAction(PetAction::Surprise, 10, 1000));
    CHECK(generic.OnDeviceState(kDeviceStateListening).action == PetAction::Surprise);
    CHECK(generic.Tick(1010));
    CHECK(generic.current().action == PetAction::Listening);
    CHECK(!generic.Tick(200000));
    generic.SetAlertEmotion("gear");
    CHECK(generic.current().action == PetAction::Notifying);
    CHECK(!generic.SetTransientAction(PetAction::Surprise, 200001, 1000));
    return 0;
}
