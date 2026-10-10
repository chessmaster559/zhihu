#include "pet_idle_policy.h"
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main() {
    PetIdlePolicy pet;
    pet.Reset(1000);
    CHECK(pet.Poll(10999) == IdleAnimation::None);
    CHECK(pet.Poll(11000) == IdleAnimation::Sway);
    CHECK(pet.Poll(15000) == IdleAnimation::Sway); // Only completion advances a clip.
    CHECK(pet.CompleteAnimation() == IdleAnimation::Play);
    CHECK(pet.CompleteAnimation() == IdleAnimation::Sway);
    CHECK(pet.Poll(30999) == IdleAnimation::Sway);
    CHECK(pet.Poll(31000) == IdleAnimation::Sleep);
    pet.RecordUserActivity(32000);
    CHECK(pet.Poll(41999) == IdleAnimation::None);
    CHECK(pet.Poll(42000) == IdleAnimation::Sway);
    pet.RecordUserActivity(45000);
    CHECK(pet.active() == IdleAnimation::Sway); // Speech does not cut ambient animation.
    CHECK(pet.Poll(74999) == IdleAnimation::Sway);
    CHECK(pet.Poll(75000) == IdleAnimation::Sleep);
    pet.Reset(80000);
    pet.EnterIdle(84000); // Greeting end starts 10s idle, not another 30s silence.
    CHECK(pet.Poll(93999) == IdleAnimation::None);
    CHECK(pet.Poll(94000) == IdleAnimation::Sway);
    CHECK(pet.Poll(110000) == IdleAnimation::Sleep);
    pet.Reset(120000);
    pet.SleepNow();
    CHECK(pet.Poll(120001) == IdleAnimation::Sleep);
    pet.RecordUserActivity(120002);
    CHECK(pet.active() == IdleAnimation::None);
    const uint32_t start = UINT32_MAX - 5000;
    pet.Reset(start);
    CHECK(pet.Poll(start + 9999) == IdleAnimation::None);
    CHECK(pet.Poll(start + 10000) == IdleAnimation::Sway);
    CHECK(pet.Poll(start + 30000) == IdleAnimation::Sleep);
    return 0;
}