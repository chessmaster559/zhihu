#ifndef _BOX_AUDIO_CODEC_H
#define _BOX_AUDIO_CODEC_H

#include "adc_pdm_signal_policy.h"
#include "audio_codec.h"
#include "capture_retry_policy.h"

#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <array>
#include <mutex>

class AdcPdmAudioCodec : public AudioCodec {
private:
    esp_codec_dev_handle_t input_dev_ = nullptr;
    const audio_codec_data_if_t* input_data_if_ = nullptr;
    CaptureRetryPolicy capture_retry_;
    AdcDcBlocker dc_blocker_;
    int32_t output_gain_q15_ = 32768;
    uint64_t input_stats_start_us_ = 0;
    uint32_t input_stats_samples_ = 0;
    int64_t input_stats_raw_sum_ = 0;
    uint64_t input_stats_ac_square_ = 0;
    uint32_t input_errors_ = 0;
    gpio_num_t pa_ctrl_pin_ = GPIO_NUM_NC;
    gpio_num_t pdm_p_pin_ = GPIO_NUM_NC;
    gpio_num_t pdm_n_pin_ = GPIO_NUM_NC;
    std::mutex output_mutex_;
    std::array<int16_t, 240> output_buffer_{};
    int ramp_samples_ = 0;
    bool tx_running_ = false;
    void RoutePdm(bool enabled);
    void StopOutput();  // Requires output_mutex_.

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    AdcPdmAudioCodec(int input_sample_rate, int output_sample_rate, uint32_t adc_mic_channel,
                     gpio_num_t pdm_speak_p, gpio_num_t pdm_speak_n, gpio_num_t pa_ctl);
    virtual ~AdcPdmAudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
    // Bridge short network gaps without toggling PA; still stop after silence.
    // This exceeds the 60 ms DMA ring and does not delay PCM delivery.
    uint32_t output_idle_timeout_ms() const override { return 300; }
    uint32_t input_task_stack_bytes() const override { return 6144; }
    void Start();
};

#endif  // _BOX_AUDIO_CODEC_H
