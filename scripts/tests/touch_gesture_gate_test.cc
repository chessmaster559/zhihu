#include "touch_gesture_gate.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main() {
    TouchGestureGate gesture;
    CHECK(!gesture.Release());
    CHECK(!gesture.Press(-1, 20, 284, 240));
    CHECK(!gesture.Release());
    CHECK(!gesture.Press(20, -1, 284, 240));
    CHECK(!gesture.Release());
    CHECK(!gesture.Press(284, 20, 284, 240));
    CHECK(!gesture.Release());
    CHECK(!gesture.Press(20, 240, 284, 240));
    CHECK(!gesture.Release());
    CHECK(gesture.Press(0, 0, 284, 240));
    CHECK(gesture.Release());
    CHECK(!gesture.Release());  // A press authorizes exactly one release.
    CHECK(gesture.Press(283, 239, 284, 240));
    CHECK(gesture.Release());
    // A rejected new press cannot inherit the previous accepted gesture.
    CHECK(gesture.Press(20, 20, 284, 240));
    CHECK(!gesture.Press(4095, 4095, 284, 240));
    CHECK(!gesture.Release());
    CHECK(!gesture.Press(0, 0, 0, 240));
    CHECK(!gesture.Release());
    return 0;
}
