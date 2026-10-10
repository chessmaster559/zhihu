#pragma once

#include <stdint.h>

// Owned exclusively by AudioOutputTask; never used by the esp_timer task.
class AudioOutputLifecycle {
public:
    void BeginWrite() { writing_ = true; }
    void CompleteWrite(uint64_t now_us) {
        writing_ = false;
        last_write_us_ = now_us;
    }
    bool CanDisable(uint64_t now_us, uint32_t idle_ms, bool playback_drained,
                    bool duplex_input_active) const {
        return !writing_ && playback_drained && !duplex_input_active &&
               now_us - last_write_us_ >= static_cast<uint64_t>(idle_ms) * 1000;
    }

private:
    bool writing_ = false;
    uint64_t last_write_us_ = 0;
};
