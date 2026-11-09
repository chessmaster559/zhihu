#include "adc_pdm_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "touch_gesture_gate.h"
#include "wifi_board.h"
#if CONFIG_USE_BME690_NOSE
#include "bme690_nose.h"
#endif
#if CONFIG_USE_KANSHAN_PET && !CONFIG_USE_BME690_NOSE
#include "bmi270_pet_motion.h"
#endif
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <wifi_station.h>
#include "mcp_server.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include "display/lcd_display.h"
#include "esp_lcd_ili9341.h"

#include "display/emote_display.h"

#include "anim_player.h"
#include "assets/lang_config.h"
#include "driver/rmt_tx.h"
#include "i2c_device.h"
#include "led_strip.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "sdkconfig.h"

constexpr char TAG[] = "ESP_SensairShuttle";

static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
    // {cmd, { data }, data_size, delay_ms}
    {0x11, NULL, 0, 120},                                     // Sleep Out
    {0x36, (uint8_t[]){0x00}, 1, 0},                          // Memory Data Access Control
    {0x3A, (uint8_t[]){0x05}, 1, 0},                          // Interface Pixel Format (16-bit)
    {0xB2, (uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5, 0},  // Porch Setting
    {0xB7, (uint8_t[]){0x05}, 1, 0},                          // Gate Control
    {0xBB, (uint8_t[]){0x21}, 1, 0},                          // VCOM Setting
    {0xC0, (uint8_t[]){0x2C}, 1, 0},                          // LCM Control
    {0xC2, (uint8_t[]){0x01}, 1, 0},                          // VDV and VRH Command Enable
    {0xC3, (uint8_t[]){0x15}, 1, 0},                          // VRH Set
    {0xC6, (uint8_t[]){0x0F}, 1, 0},                          // Frame Rate Control
    {0xD0, (uint8_t[]){0xA7}, 1, 0},                          // Power Control 1
    {0xD0, (uint8_t[]){0xA4, 0xA1}, 2, 0},                    // Power Control 1
    {0xD6, (uint8_t[]){0xA1}, 1, 0},                          // Gate output GND in sleep mode
    {0xE0,
     (uint8_t[]){0xF0, 0x05, 0x0E, 0x08, 0x0A, 0x17, 0x39, 0x54, 0x4E, 0x37, 0x12, 0x12, 0x31,
                 0x37},
     14, 0},  // Positive Gamma Control
    {0xE1,
     (uint8_t[]){0xF0, 0x10, 0x14, 0x0D, 0x0B, 0x05, 0x39, 0x44, 0x4D, 0x38, 0x14, 0x14, 0x2E,
                 0x35},
     14, 0},                                      // Negative Gamma Control
    {0xE4, (uint8_t[]){0x23, 0x00, 0x00}, 3, 0},  // Gate position control
    {0x21, NULL, 0, 0},                           // Display Inversion On
    {0x29, NULL, 0, 0},                           // Display On
    {0x2C, NULL, 0, 0},                           // Memory Write
};

class Cst816d : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };

    enum TouchEvent { TOUCH_NONE, TOUCH_PRESS, TOUCH_RELEASE, TOUCH_HOLD };

    Cst816d(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        read_buffer_ = new uint8_t[6];
        was_touched_ = false;
        press_count_ = 0;
    }

    ~Cst816d() { delete[] read_buffer_; }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    const TouchPoint_t& GetTouchPoint() { return tp_; }

    TouchEvent CheckTouchEvent() {
        bool is_touched = (tp_.num > 0);
        TouchEvent event = TOUCH_NONE;

        if (is_touched && !was_touched_) {
            // Press event (transition from not touched to touched)
            press_count_++;
            event = TOUCH_PRESS;
            ESP_LOGI(TAG, "TOUCH PRESS - count: %d, x: %d, y: %d", press_count_, tp_.x, tp_.y);
        } else if (!is_touched && was_touched_) {
            // Release event (transition from touched to not touched)
            event = TOUCH_RELEASE;
            ESP_LOGI(TAG, "TOUCH RELEASE - total presses: %d", press_count_);
        } else if (is_touched && was_touched_) {
            // Continuous touch (hold)
            event = TOUCH_HOLD;
            ESP_LOGD(TAG, "TOUCH HOLD - x: %d, y: %d", tp_.x, tp_.y);
        }

        // Update previous state
        was_touched_ = is_touched;
        return event;
    }

    int GetPressCount() const { return press_count_; }

    void ResetPressCount() { press_count_ = 0; }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;

    // Touch state tracking
    bool was_touched_;
    int press_count_;
};

class EspSensairShuttle : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Cst816d* cst816d_;
    Display* display_ = nullptr;
    Button boot_button_;
#if CONFIG_USE_KANSHAN_PET || CONFIG_USE_BME690_NOSE
    bool shuttle_pins_ready_ = false;
#endif
#if CONFIG_USE_BME690_NOSE
    Bme690Nose nose_;
#endif
#if CONFIG_USE_KANSHAN_PET && !CONFIG_USE_BME690_NOSE
    Bmi270PetMotion pet_motion_;
#endif

    void InitializeI2c() {
#if CONFIG_USE_KANSHAN_PET || CONFIG_USE_BME690_NOSE
        // Configure the shared Shuttle straps once, before starting any sensor.
        // Preload CS high before enabling output: a low-to-high pulse can lock
        // the BMI270 into SPI. SDO low selects BME690=0x76 / BMI270=0x68.
        esp_err_t strap_error = gpio_set_level(SHUTTLE_CS_GPIO, 1);
        if (strap_error == ESP_OK)
            strap_error = gpio_set_level(SHUTTLE_SDO_GPIO, 0);
        gpio_config_t straps{};
        straps.pin_bit_mask = (1ULL << SHUTTLE_CS_GPIO) | (1ULL << SHUTTLE_SDO_GPIO);
        straps.mode = GPIO_MODE_OUTPUT;
        if (strap_error == ESP_OK)
            strap_error = gpio_config(&straps);
        shuttle_pins_ready_ = strap_error == ESP_OK;
        if (!shuttle_pins_ready_)
            ESP_LOGE(TAG, "Shuttle straps failed: %s; optional sensors disabled",
                     esp_err_to_name(strap_error));
#endif
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = LCD_TP_SDA,
            .scl_io_num = LCD_TP_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    static void touch_event_task(void* arg) {
        Cst816d* touchpad = static_cast<Cst816d*>(arg);
        if (touchpad == nullptr) {
            ESP_LOGE(TAG, "Invalid touchpad pointer in touch_event_task");
            vTaskDelete(NULL);
            return;
        }

        TouchGestureGate gesture;
        while (true) {
            touchpad->UpdateTouchPoint();
            auto touch_event = touchpad->CheckTouchEvent();

            if (touch_event == Cst816d::TOUCH_PRESS) {
                const auto& point = touchpad->GetTouchPoint();
                if (gesture.Press(point.x, point.y, DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
#if CONFIG_USE_KANSHAN_PET
                    Application::GetInstance().NotifyPetTouch();
#endif
                } else {
                    ESP_LOGW(TAG, "Ignoring invalid touch coordinates: x=%d y=%d", point.x,
                             point.y);
                }
            }

            if (touch_event == Cst816d::TOUCH_RELEASE && gesture.Release()) {
                auto& app = Application::GetInstance();
                auto& board = (EspSensairShuttle&)Board::GetInstance();

                if (app.GetDeviceState() == kDeviceStateStarting) {
                    board.EnterWifiConfigMode();
                } else {
                    app.ToggleChatState();
                }
            }

            vTaskDelay(pdMS_TO_TICKS(50));  // Poll every 50ms
        }
    }

    void InitializeCst816dTouchPad() {
        cst816d_ = new Cst816d(i2c_bus_, 0x15);
        xTaskCreate(touch_event_task, "touch_task", 2 * 1024, cst816d_, 5, NULL);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                ESP_LOGI(TAG, "Boot button pressed, enter WiFi configuration mode");
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * 10 * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        const ili9341_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
        };

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void*)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_set_gap(panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        ESP_LOGI(TAG, "LCD panel create success, %p", panel);

#ifdef CONFIG_USE_EMOTE_MESSAGE_STYLE
        display_ = new emote::EmoteDisplay(panel, panel_io, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#else
        display_ = new SpiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
#endif
    }

public:
    EspSensairShuttle() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeCst816dTouchPad();
        InitializeButtons();
        InitializeSpi();
        InitializeLcdDisplay();
#if CONFIG_USE_KANSHAN_PET && !CONFIG_USE_BME690_NOSE
        // The original Shuttle socket holds one sensor daughterboard at a time.
        // Nose mode keeps pet animations/touch, without probing an absent IMU.
        if (shuttle_pins_ready_)
            pet_motion_.Start(i2c_bus_);
#endif
#if CONFIG_USE_BME690_NOSE
        if (shuttle_pins_ready_)
            nose_.Start(i2c_bus_);
        McpServer::GetInstance().AddTool(
            "self.pet.nose.get_status",
            "读取刘看山鼻子 BME690 的最新缓存读数，用于闻一闻、查询温湿度或检查传感器。"
            "必须根据 hardware_ok、fresh、gas_valid、heater_stable 和 self_test 判断可用性。"
            "gas_resistance_ohm 是原始气体电阻，不是空气质量分数；未接入 BSEC 和气味训练模型，"
            "不得据此宣称识别了具体气味、甲醛浓度、真实 CO2 或空气安全。",
            PropertyList(), [this](const PropertyList&) -> ToolResult {
                if (auto* json = nose_.GetStatusJson())
                    return json;
                return std::unexpected(std::string("BME690 status allocation failed"));
            });
#endif
    }

    virtual AudioCodec* GetAudioCodec() override {
        static AdcPdmAudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                            AUDIO_ADC_MIC_CHANNEL, AUDIO_PDM_SPEAK_P_GPIO,
                                            AUDIO_PDM_SPEAK_N_GPIO, AUDIO_PA_CTL_GPIO);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    Cst816d* GetTouchpad() { return cst816d_; }
};

DECLARE_BOARD(EspSensairShuttle);
