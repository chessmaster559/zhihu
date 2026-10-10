#include "motion_detector.h"

// Windows freestanding host executable, no IDF or C++ runtime needed.
extern "C" {
int _fltused = 0;
}

#define CHECK(condition)     \
    do {                     \
        if (!(condition))    \
            return __LINE__; \
    } while (0)

static PetMotion Feed(MotionDetector& detector, MotionSample sample, int count, uint32_t& now) {
    PetMotion found = PetMotion::None;
    for (int i = 0; i < count; ++i) {
        auto event = detector.Update(sample, now);
        if (event != PetMotion::None)
            found = event;
        now += 20;
    }
    return found;
}

int main() {
    MotionDetector detector;
    uint32_t now = 0;
    const MotionSample level{0, 0, 1, 0, 0, 0};
    CHECK(Feed(detector, level, 49, now) == PetMotion::None);
    CHECK(!detector.calibrated());
    Feed(detector, level, 1, now);
    CHECK(detector.calibrated());
    CHECK(Feed(detector, level, 100, now) == PetMotion::None);
    CHECK(Feed(detector, {0.7f, 0, 0.714f, 0, 0, 0}, 10, now) == PetMotion::None);
    CHECK(Feed(detector, {0.7f, 0, 0.714f, 0, 0, 0}, 25, now) == PetMotion::TiltRight);
    CHECK(Feed(detector, {0.7f, 0, 0.714f, 0, 0, 0}, 55, now) == PetMotion::TiltRight);
    CHECK(Feed(detector, level, 40, now) == PetMotion::Level);
    CHECK(Feed(detector, {-0.7f, 0, 0.714f, 0, 0, 0}, 40, now) == PetMotion::TiltLeft);
    CHECK(Feed(detector, {0, 0, -1, 0, 0, 0}, 60, now) == PetMotion::UpsideDown);
    CHECK(Feed(detector, level, 60, now) == PetMotion::Level);

    detector.Reset();
    Feed(detector, level, 60, now);
    int shakes = 0;
    for (int pulse = 0; pulse < 6; ++pulse) {
        if (Feed(detector, {pulse % 2 ? -2.5f : 2.5f, 0, 1, 0, 0, 0}, 1, now) == PetMotion::Shake)
            ++shakes;
        if (Feed(detector, level, 4, now) == PetMotion::Shake)
            ++shakes;
    }
    CHECK(shakes == 1);  // Three peaks trigger, cooldown suppresses the next three.
    Feed(detector, level, 100, now);
    shakes = 0;
    for (int pulse = 0; pulse < 3; ++pulse) {
        if (Feed(detector, {pulse % 2 ? -2.5f : 2.5f, 0, 1, 0, 0, 0}, 1, now) == PetMotion::Shake)
            ++shakes;
        Feed(detector, level, 4, now);
    }
    CHECK(shakes == 1);

    detector.Reset();
    now = UINT32_MAX - 500;
    Feed(detector, level, 60, now);  // Calibration across uptime wrap.
    CHECK(detector.calibrated());
    CHECK(Feed(detector, {0.7f, 0, 0.714f, 0, 0, 0}, 40, now) == PetMotion::TiltRight);
    now += 1000;
    CHECK(Feed(detector, level, 1, now) == PetMotion::None);
    CHECK(!detector.calibrated());  // Sampling gap invalidates the baseline.

    detector.Reset();
    Feed(detector, {1, 0, 0, 0, 0, 0}, 60, now);  // Sensor X vertical: use Y fallback.
    CHECK(detector.calibrated());
    CHECK(Feed(detector, {0.714f, 0.7f, 0, 0, 0, 0}, 40, now) == PetMotion::TiltRight);
    detector.Reset();
    Feed(detector, {0, 0, 1, 30, 0, 0}, 100, now);
    CHECK(!detector.calibrated());  // Rotating device cannot establish a stationary baseline.
    Feed(detector, level, 60, now);
    CHECK(detector.calibrated());
    CHECK(Feed(detector, {9, 0, 1, 0, 0, 0}, 1, now) == PetMotion::None);
    CHECK(!detector.calibrated());
    detector.Reset();
    Feed(detector, {0, 0, 0, 0, 0, 0}, 100, now);
    CHECK(!detector.calibrated());
    return 0;
}
