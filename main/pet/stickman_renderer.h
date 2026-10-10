#pragma once

#include <lvgl.h>
#include "pet_controller.h"

// All methods run under the LVGL display lock (timer callbacks already hold it).
class StickmanRenderer {
public:
    explicit StickmanRenderer(lv_obj_t* parent);
    ~StickmanRenderer();
    void Show(PetAction action);
    void Hide();
    void Pause(bool paused);
    void SetBackgroundColor(lv_color_t color);

private:
    void Draw();
    lv_point_precise_t Point(float x, float y, float lean, int bounce) const;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* head_ = nullptr;
    lv_obj_t* lines_[5]{};
    lv_point_precise_t points_[5][3]{};
    lv_obj_t* caption_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    PetAction action_ = PetAction::Idle;
    bool visible_ = false;
    bool paused_ = false;
    uint32_t started_ms_ = 0;
    uint32_t stats_ms_ = 0;
    uint32_t last_frame_ms_ = 0;
    uint32_t frames_ = 0;
    uint32_t max_interval_ms_ = 0;
};
