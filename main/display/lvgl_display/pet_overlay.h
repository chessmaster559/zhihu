#pragma once

#include <lvgl.h>

// All calls run under the display lock; the effect timer runs in LVGL's task.
// Widgets and text buffers are reused, independently of the EAF player.
class PetOverlay {
public:
    enum class Indicator { Hidden, Listening, Hearing, Thinking };
    PetOverlay(lv_obj_t* screen, int width, int height, const lv_font_t* font);
    ~PetOverlay();
    void SetMessage(const char* role, const char* text);
    void ClearMessages();
    void SetHidden(bool hidden);
    void SetSleeping(bool sleeping);
    void ShowHearts();
    void SetTheme(const lv_font_t* font, lv_color_t foreground, lv_color_t background);
    void SetSceneColors(lv_color_t foreground, lv_color_t background, bool night);
    void SetPetPosition(int center_x, int center_y);
    void SetIndicator(Indicator indicator);

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* bubble_ = nullptr;
    lv_obj_t* tail_ = nullptr;
    lv_obj_t* speech_ = nullptr;
    lv_obj_t* bottom_ = nullptr;
    lv_obj_t* user_ = nullptr;
    lv_obj_t* system_ = nullptr;
    lv_obj_t* sleep_[3]{};
    lv_obj_t* hearts_[3]{};
    lv_obj_t* indicator_ = nullptr;
    Indicator indicator_state_ = Indicator::Hidden;
    lv_timer_t* timer_ = nullptr;
    char speech_text_[768]{};
    char user_text_[384]{};
    char system_text_[384]{};
    int width_;
    int height_;
    int pet_x_;
    int pet_y_;
    bool hidden_ = false;
    bool sleeping_ = false;
    bool hearts_active_ = false;
    uint32_t sleep_start_ = 0;
    uint32_t hearts_start_ = 0;
    void UpdateVisibility();
    void UpdateEffects();
    void UpdateTimer();
    static void DrawTail(lv_event_t* event);
    static void DrawHeart(lv_event_t* event);
    static void DrawIndicator(lv_event_t* event);
};
