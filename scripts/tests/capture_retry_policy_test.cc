#include "capture_retry_policy.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main() {
    CaptureRetryPolicy input;
    CHECK(input.CanStart(0));
    input.StartFailed(1000000);
    CHECK(!input.CanStart(5999999));
    CHECK(input.CanStart(6000000));
    // Repeated initialization failure renews backoff; success resets the state.
    input.StartFailed(6000000);
    CHECK(!input.CanStart(10999999));
    CHECK(input.CanStart(11000000));
    input.Started();
    CHECK(input.CanStart(11000000));
    for (int i = 0; i < 9; ++i)
        CHECK(!input.ReadFailed(12000000 + i * 10000));
    CHECK(input.consecutive_errors() == 9);
    // A valid frame breaks the consecutive-error sequence.
    input.ReadSucceeded();
    CHECK(input.consecutive_errors() == 0);
    for (int i = 0; i < 9; ++i)
        CHECK(!input.ReadFailed(13000000 + i * 10000));
    CHECK(input.ReadFailed(13100000));
    CHECK(!input.CanStart(18099999));
    CHECK(input.CanStart(18100000));
    input.Started();
    CHECK(input.consecutive_errors() == 0);
    CHECK(!input.ReadFailed(18110000));
    // Use the monotonic microsecond clock without 32-bit millisecond truncation.
    const uint64_t long_uptime = static_cast<uint64_t>(UINT32_MAX) * 1000 + 20000;
    input.StartFailed(long_uptime);
    CHECK(!input.CanStart(long_uptime + 4999999));
    CHECK(input.CanStart(long_uptime + 5000000));
    return 0;
}
