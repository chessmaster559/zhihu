#include "pet_scene_policy.h"

#define CHECK(condition)     \
    do {                     \
        if (!(condition))    \
            return __LINE__; \
    } while (0)

int main() {
    CHECK(UseNightPetScene(false, 0));   // Midnight remains night.
    CHECK(UseNightPetScene(false, 7));   // 07:59 remains night.
    CHECK(!UseNightPetScene(false, 8));  // 08:00 wakes into day.
    CHECK(!UseNightPetScene(false, 18));
    CHECK(UseNightPetScene(false, 19));  // 19:00 starts night.
    CHECK(UseNightPetScene(false, 23));
    CHECK(UseNightPetScene(true, 12));    // Daytime sleep overrides the clock.
    CHECK(!UseNightPetScene(false, 12));  // Wake during daytime restores day.
    CHECK(UseNightPetScene(true, 8));     // Sleeping at 08:00 stays night.
    CHECK(UseNightPetScene(false, 20));   // Wake during nighttime stays night.
    CHECK(!UseNightPetScene(false, -1));  // Unset clock does not imply midnight.
    CHECK(UseNightPetScene(true, -1));
    CHECK(!UseNightPetScene(false, 24));  // Reject invalid hours.
    return 0;
}
