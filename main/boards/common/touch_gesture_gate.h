#pragma once

// Match release to an accepted press. Release coordinates need not be valid.
class TouchGestureGate {
public:
    bool Press(int x, int y, int width, int height) {
        accepted_ = x >= 0 && x < width && y >= 0 && y < height;
        return accepted_;
    }
    bool Release() {
        const bool accepted = accepted_;
        accepted_ = false;
        return accepted;
    }

private:
    bool accepted_ = false;
};
