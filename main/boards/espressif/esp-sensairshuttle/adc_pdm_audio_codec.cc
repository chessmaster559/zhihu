#include "adc_pdm_audio_codec.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>
#include <cmath>
#include "config.h"
#include "driver/i2s_pdm.h"
#include "esp_private/gpio.h"
#include "settings.h"
#include "soc/gpio_sig_map.h"

namespace {
constexpr char TAG[] = "AdcPdmAudioCodec";
constexpr int kFrameSamples = 240;
constexpr int kDmaFrames = 6;
constexpr int kWriteTimeoutMs = 100;
const int16_t kSilence[kFrameSamples] = {};
}  // namespace

AdcPdmAudioCodec::AdcPdmAudioCodec(int input_sample_rate, int output_sample_rate,
                                   uint32_t adc_mic_channel, gpio_num_t pdm_speak_p,
                                   gpio_num_t pdm_speak_n, gpio_num_t pa_ctl)
    : pa_ctrl_pin_(pa_ctl), pdm_p_pin_(pdm_speak_p), pdm_n_pin_(pdm_speak_n) {
    input_reference_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    output_volume_ = 100;

    // MIC-DMA: official esp_codec_dev ADC interface owns the continuous DMA driver.
    // Start/stop remain in AudioInputTask; never disable it from a timer callback.
    audio_codec_adc_cfg_t cfg = {};
    cfg.continuous_cfg.max_store_buf_size = 4096;
    cfg.continuous_cfg.conv_frame_size = 640;  // 160 ADC records, 10 ms at 16 kHz.
    cfg.continuous_cfg.sample_freq_hz = static_cast<uint32_t>(input_sample_rate);
    cfg.continuous_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    cfg.continuous_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    cfg.continuous_cfg.pattern_num = 1;
    cfg.continuous_cfg.cfg_mode = AUDIO_CODEC_ADC_CFG_MODE_SINGLE_UNIT;
    cfg.continuous_cfg.cfg.single_unit.unit_id = ADC_UNIT_1;
    cfg.continuous_cfg.cfg.single_unit.atten = ADC_ATTEN_DB_12;
    cfg.continuous_cfg.cfg.single_unit.bit_width = ADC_BITWIDTH_12;
    cfg.continuous_cfg.cfg.single_unit.channel_id[0] = static_cast<uint8_t>(adc_mic_channel);
    input_data_if_ = audio_codec_new_adc_data(&cfg);
    ESP_ERROR_CHECK(input_data_if_ ? ESP_OK : ESP_ERR_NO_MEM);
    esp_codec_dev_cfg_t dev_cfg = {};
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.data_if = input_data_if_;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    ESP_ERROR_CHECK(input_dev_ ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "ADC DMA microphone ready: ADC1_CH%lu, %d Hz", adc_mic_channel,
             input_sample_rate);

    // PA-ORDER: assert shutdown before initializing either PDM output.
    if (pa_ctrl_pin_ != GPIO_NUM_NC) {
        ESP_ERROR_CHECK(gpio_set_level(pa_ctrl_pin_, 0));
        ESP_ERROR_CHECK(gpio_set_direction(pa_ctrl_pin_, GPIO_MODE_OUTPUT));
    }
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = kDmaFrames;
    chan.dma_frame_num = kFrameSamples;
    chan.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx_handle_, nullptr));

    i2s_pdm_tx_config_t pdm = {};
    // PA-CLOCK: official DAC profile keeps the PDM bit rate at 6.144 MHz.
    // Do not overwrite fs with the old codec-mode value (480): at 24 kHz
    // that halves the bit rate and reduces the analogue filter's margin.
    pdm.clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(static_cast<uint32_t>(output_sample_rate));
    // Analog RC/PA input needs DAC mode, rather than codec clock-edge slots.
    pdm.slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    // Preserve the effective HP gain previously selected by esp_codec_dev.
    pdm.slot_cfg.hp_scale = I2S_PDM_SIG_SCALING_DIV_2;
    pdm.gpio_cfg.clk = GPIO_NUM_NC;
    pdm.gpio_cfg.dout = pdm_p_pin_;
    pdm.gpio_cfg.dout2 = GPIO_NUM_NC;
    ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(tx_handle_, &pdm));
    RoutePdm(false);
    const uint32_t nominal_pdm_hz = static_cast<uint64_t>(output_sample_rate) * 64 *
                                    pdm.clk_cfg.up_sample_fp / pdm.clk_cfg.up_sample_fs;
    ESP_LOGI(TAG,
             "PDM differential DAC: P=%d N=%d PA=%d, configured_pdm_hz=%lu fp=%lu fs=%lu "
             "bclk_div=%lu idle_ms=%lu",
             pdm_p_pin_, pdm_n_pin_, pa_ctrl_pin_, static_cast<unsigned long>(nominal_pdm_hz),
             static_cast<unsigned long>(pdm.clk_cfg.up_sample_fp),
             static_cast<unsigned long>(pdm.clk_cfg.up_sample_fs),
             static_cast<unsigned long>(pdm.clk_cfg.bclk_div),
             static_cast<unsigned long>(output_idle_timeout_ms()));
}

void AdcPdmAudioCodec::RoutePdm(bool enabled) {
    // PA-MATRIX: fan out ONE bitstream; invert N data, not its output enable.
    // Select each actual pin via GPIO API; never touch the GPIO10 mux.
    const gpio_num_t pins[] = {pdm_p_pin_, pdm_n_pin_};
    for (int i = 0; i < 2; ++i) {
        if (pins[i] == GPIO_NUM_NC)
            continue;
        ESP_ERROR_CHECK(gpio_set_direction(pins[i], GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_drive_capability(pins[i], GPIO_DRIVE_CAP_0));
        ESP_ERROR_CHECK(gpio_set_level(pins[i], 0));
        ESP_ERROR_CHECK(gpio_matrix_output(pins[i], enabled ? I2SO_SD_OUT_IDX : SIG_GPIO_OUT_IDX,
                                           enabled && i == 1, false));
    }
}

void AdcPdmAudioCodec::StopOutput() {
    // PA-QUIET: PA off first, stop TX second, clamp BOTH pins low last.
    if (pa_ctrl_pin_ != GPIO_NUM_NC)
        gpio_set_level(pa_ctrl_pin_, 0);
    if (tx_running_) {
        ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
        tx_running_ = false;
    }
    RoutePdm(false);
    AudioCodec::EnableOutput(false);
}

AdcPdmAudioCodec::~AdcPdmAudioCodec() {
    std::lock_guard<std::mutex> lock(output_mutex_);
    StopOutput();
    if (tx_handle_)
        i2s_del_channel(tx_handle_);
    if (input_dev_) {
        esp_codec_dev_close(input_dev_);
        esp_codec_dev_delete(input_dev_);
    }
    if (input_data_if_)
        audio_codec_delete_data_if(input_data_if_);
}

void AdcPdmAudioCodec::SetOutputVolume(int volume) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    AudioCodec::SetOutputVolume(std::clamp(volume, 0, 100));
    output_gain_q15_ = CodecVolumeGainQ15(output_volume_);
}

void AdcPdmAudioCodec::EnableInput(bool enable) {
    if (!input_dev_ || enable == input_enabled_)
        return;
    if (enable) {
        const uint64_t now = esp_timer_get_time();
        if (!capture_retry_.CanStart(now))
            return;
        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = 16;
        fs.channel = 1;
        fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0);
        fs.sample_rate = input_sample_rate_;
        const int err = esp_codec_dev_open(input_dev_, &fs);
        if (err != ESP_CODEC_DEV_OK) {
            capture_retry_.StartFailed(now);
            ESP_LOGE(TAG, "ADC open failed: %d; retry in 5s", err);
            return;
        }
        capture_retry_.Started();
        dc_blocker_.Reset();
        input_stats_start_us_ = now;
        input_stats_samples_ = 0;
        input_stats_raw_sum_ = 0;
        input_stats_ac_square_ = 0;
    } else {
        const int err = esp_codec_dev_close(input_dev_);
        if (err != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "ADC close failed: %d", err);
            return;
        }
    }
    AudioCodec::EnableInput(enable);
}

void AdcPdmAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (enable == output_enabled_)
        return;
    if (!enable) {
        StopOutput();
        return;
    }
    RoutePdm(true);
    // PA-PRIME: preload zero PCM before enabling the PDM hardware.
    for (int i = 0; i < kDmaFrames; ++i) {
        size_t loaded = 0;
        ESP_ERROR_CHECK(i2s_channel_preload_data(tx_handle_, kSilence, sizeof(kSilence), &loaded));
        if (!loaded)
            break;
    }
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    tx_running_ = true;
    // Bounded DMA writes settle the PDM/RC network with PA off, without sleeps.
    for (int i = 0; i < 2; ++i) {
        size_t written = 0;
        const auto err =
            i2s_channel_write(tx_handle_, kSilence, sizeof(kSilence), &written, kWriteTimeoutMs);
        if (err != ESP_OK || written != sizeof(kSilence)) {
            ESP_LOGE(TAG, "PDM silence priming failed: %s", esp_err_to_name(err));
            StopOutput();
            return;
        }
    }
    if (pa_ctrl_pin_ != GPIO_NUM_NC)
        gpio_set_level(pa_ctrl_pin_, 1);
    ramp_samples_ = kFrameSamples;
    AudioCodec::EnableOutput(true);
}

int AdcPdmAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_dev_ || !input_enabled_)
        return 0;
    const int err = esp_codec_dev_read(input_dev_, dest, samples * sizeof(int16_t));
    const uint64_t now = esp_timer_get_time();
    if (err != ESP_CODEC_DEV_OK) {
        ++input_errors_;
        if (capture_retry_.ReadFailed(now)) {
            ESP_LOGE(TAG, "ADC read repeatedly failed: %d; restart after 5s", err);
            EnableInput(false);
        }
        return 0;
    }
    capture_retry_.ReadSucceeded();
    for (int i = 0; i < samples; ++i) {
        input_stats_raw_sum_ += dest[i];
        dest[i] = dc_blocker_.Process(dest[i]);
        input_stats_ac_square_ += static_cast<int64_t>(dest[i]) * dest[i];
    }
    input_stats_samples_ += samples;
    const uint64_t elapsed = now - input_stats_start_us_;
    if (elapsed >= 5000000 && input_stats_samples_) {
        ESP_LOGI(TAG, "ADC PCM: delivered_hz=%lu raw_mean=%.1f ac_rms=%.1f errors=%lu",
                 static_cast<unsigned long>(input_stats_samples_ * 1000000ULL / elapsed),
                 static_cast<double>(input_stats_raw_sum_) / input_stats_samples_,
                 std::sqrt(static_cast<double>(input_stats_ac_square_) / input_stats_samples_),
                 static_cast<unsigned long>(input_errors_));
        input_stats_start_us_ = now;
        input_stats_samples_ = 0;
        input_stats_raw_sum_ = 0;
        input_stats_ac_square_ = 0;
    }
    return samples;
}

int AdcPdmAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (!output_enabled_)
        return 0;
    int delivered = 0;
    while (delivered < samples) {
        const int count = std::min(kFrameSamples, samples - delivered);
        for (int i = 0; i < count; ++i) {
            const int ramp = ramp_samples_ ? kFrameSamples - ramp_samples_-- + 1 : kFrameSamples;
            const int32_t scaled =
                static_cast<int32_t>(data[delivered + i]) * output_gain_q15_ / 32768;
            output_buffer_[i] = scaled * ramp / kFrameSamples;
        }
        size_t written = 0;
        const auto err = i2s_channel_write(tx_handle_, output_buffer_.data(),
                                           count * sizeof(int16_t), &written, kWriteTimeoutMs);
        delivered += written / sizeof(int16_t);
        if (err != ESP_OK || written != count * sizeof(int16_t)) {
            ESP_LOGE(TAG, "PDM write failed: %s", esp_err_to_name(err));
            StopOutput();
            break;
        }
    }
    return delivered;
}

void AdcPdmAudioCodec::Start() {
    Settings settings("audio", false);
    output_volume_ = std::clamp<int>(settings.GetInt("output_volume", output_volume_), 0, 100);
    output_gain_q15_ = CodecVolumeGainQ15(output_volume_);
    // TX stays stopped until AudioOutputTask has actual audio.
    ESP_LOGI(TAG, "Audio codec started with PA muted, volume=%d gain_q15=%ld (legacy dB curve)",
             output_volume_, static_cast<long>(output_gain_q15_));
}
