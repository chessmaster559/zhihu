#pragma once
#include <stdint.h>

// Match esp_codec_dev's default -50..0 dB curve; volume zero is mute.
// Q15 values are evaluated once, avoiding pow()/allocations in the PCM path.
inline int32_t CodecVolumeGainQ15(int volume) {
    static constexpr int32_t gains[] = {
        0,     109,   116,   123,   130,   138,   146,   155,   164,   173,   184,   195,   206,
        219,   231,   245,   260,   275,   292,   309,   327,   347,   367,   389,   412,   436,
        462,   490,   519,   550,   582,   617,   653,   692,   733,   777,   823,   871,   923,
        978,   1036,  1097,  1162,  1231,  1304,  1381,  1463,  1550,  1642,  1739,  1842,  1951,
        2067,  2190,  2319,  2457,  2602,  2757,  2920,  3093,  3276,  3470,  3676,  3894,  4125,
        4369,  4628,  4902,  5193,  5501,  5827,  6172,  6538,  6925,  7335,  7770,  8230,  8718,
        9235,  9782,  10362, 10976, 11626, 12315, 13045, 13818, 14636, 15504, 16422, 17396, 18426,
        19518, 20675, 21900, 23197, 24572, 26028, 27570, 29204, 30934, 32768};
    return gains[volume < 0 ? 0 : volume > 100 ? 100 : volume];
}

class AdcDcBlocker {
public:
    void Reset() { initialized_ = false; }
    int16_t Process(int16_t sample) {
        if (!initialized_) {
            dc_ = sample;
            initialized_ = true;
        }
        // About 20 Hz at 16 kHz: remove the actual microphone bias rather than
        // assuming the analogue circuit biases the ADC at exactly half scale.
        dc_ += (sample - dc_) / 128.0f;
        const float ac = sample - dc_;
        return static_cast<int16_t>(ac < -32768 ? -32768 : ac > 32767 ? 32767 : ac);
    }

private:
    float dc_ = 0;
    bool initialized_ = false;
};
