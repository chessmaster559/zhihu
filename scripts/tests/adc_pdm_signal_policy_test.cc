#include "adc_pdm_signal_policy.h"
extern "C" {
int _fltused = 0;
}  // Freestanding Windows floating-point marker.
#define CHECK(condition)     \
    do {                     \
        if (!(condition))    \
            return __LINE__; \
    } while (0)
int main() {
    CHECK(CodecVolumeGainQ15(-10) == 0);
    CHECK(CodecVolumeGainQ15(0) == 0);
    CHECK(CodecVolumeGainQ15(70) == 5827);  // -15 dB, not 70% linear gain.
    CHECK(CodecVolumeGainQ15(100) == 32768);
    CHECK(CodecVolumeGainQ15(110) == 32768);
    for (int volume = 1; volume <= 100; ++volume)
        CHECK(CodecVolumeGainQ15(volume) > CodecVolumeGainQ15(volume - 1));
    AdcDcBlocker filter;
    for (int i = 0; i < 1000; ++i)
        CHECK(filter.Process(-25808) == 0);
    // Retain an AC signal around a large measured bias, without clipping it.
    int sum = 0;
    int magnitude = 0;
    for (int i = 0; i < 6000; ++i) {
        const int sample = filter.Process(-25808 + (i % 2 ? 1000 : -1000));
        if (i >= 2000) {
            sum += sample;
            magnitude += sample < 0 ? -sample : sample;
        }
    }
    CHECK(sum > -4000 && sum < 4000);
    CHECK(magnitude > 3900000 && magnitude < 4000000);
    for (int i = 0; i < 3000; ++i)
        filter.Process(-20000);
    const int settled = filter.Process(-20000);
    CHECK(settled >= -1 && settled <= 1);
    filter.Reset();
    CHECK(filter.Process(16000) == 0);  // Reopening capture must not inject a step.
    return 0;
}
