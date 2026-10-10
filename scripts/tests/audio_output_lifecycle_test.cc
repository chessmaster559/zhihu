#include "audio_output_lifecycle.h"

#define CHECK(condition)     \
    do {                     \
        if (!(condition))    \
            return __LINE__; \
    } while (0)

int main() {
    AudioOutputLifecycle output;
    output.BeginWrite();
    // A blocked write must survive both the old 120ms and 15s deadlines.
    CHECK(!output.CanDisable(500000, 120, true, false));
    CHECK(!output.CanDisable(16000000, 15000, true, false));
    output.CompleteWrite(500000);
    CHECK(!output.CanDisable(619999, 120, true, false));
    CHECK(output.CanDisable(620000, 120, true, false));
    // Pending decode/PCM and duplex RX keep the clock alive after the deadline.
    CHECK(!output.CanDisable(900000, 120, false, false));
    CHECK(!output.CanDisable(900000, 120, true, true));
    CHECK(output.CanDisable(900000, 120, true, false));
    // C5's 300ms hold bridges gaps which used to toggle PA at 120ms.
    output.CompleteWrite(1000000);
    CHECK(!output.CanDisable(1120000, 300, true, false));
    CHECK(!output.CanDisable(1240000, 300, true, false));
    CHECK(!output.CanDisable(1299999, 300, true, false));
    output.BeginWrite();
    CHECK(!output.CanDisable(1400000, 300, true, false));
    output.CompleteWrite(1400000);
    CHECK(!output.CanDisable(1640000, 300, true, false));
    CHECK(!output.CanDisable(1700000, 300, false, false));
    CHECK(output.CanDisable(1700000, 300, true, false));
    // Continuous 60ms PCM frames renew the idle period at write completion.
    for (uint64_t now = 1000000; now < 4000000; now += 60000) {
        output.BeginWrite();
        CHECK(!output.CanDisable(now, 120, true, false));
        output.CompleteWrite(now);
        CHECK(!output.CanDisable(now + 60000, 120, true, false));
    }
    // Preserve the default 15s timeout and uptime beyond 32-bit millisecond wrap.
    const uint64_t long_uptime = static_cast<uint64_t>(UINT32_MAX) * 1000 + 500000;
    output.CompleteWrite(long_uptime);
    CHECK(!output.CanDisable(long_uptime + 14999999, 15000, true, false));
    CHECK(output.CanDisable(long_uptime + 15000000, 15000, true, false));
    return 0;
}
