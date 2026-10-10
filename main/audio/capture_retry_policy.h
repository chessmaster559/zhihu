#pragma once

#include <stdint.h>

// Input-task-owned retry state. It never calls a driver from a timer task.
class CaptureRetryPolicy {
public:
    bool CanStart(uint64_t now_us) const { return now_us >= retry_after_us_; }
    void StartFailed(uint64_t now_us) { retry_after_us_ = now_us + 5000000; }
    void Started() {
        retry_after_us_ = 0;
        consecutive_errors_ = 0;
    }
    bool ReadFailed(uint64_t now_us) {
        if (++consecutive_errors_ < 10)
            return false;
        StartFailed(now_us);
        return true;
    }
    void ReadSucceeded() { consecutive_errors_ = 0; }
    uint32_t consecutive_errors() const { return consecutive_errors_; }

private:
    uint64_t retry_after_us_ = 0;
    uint32_t consecutive_errors_ = 0;
};
