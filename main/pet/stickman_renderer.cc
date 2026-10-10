#include "stickman_renderer.h"
#include "kanshan_bw_image.h"

#include <esp_log.h>

namespace {
constexpr char TAG[] = "StickmanPet";
constexpr const char* kNames[] = {"idle",  "thinking",  "listening",  "speaking",
                                  "alert", "setup",     "touch",      "celebrate",
                                  "shake", "tilt left", "tilt right", "upside down",
                                  "sway", "playing", "sleeping"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<unsigned>(PetAction::Count));
}  // namespace

StickmanRenderer::StickmanRenderer(lv_obj_t* parent) {
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 128, 154);
    // REVIEW[FIX-UI-01] 每帧重绘固定的不透明舞台，旧肢体像素由背景覆盖。
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(root_, LV_ALIGN_CENTER, 0, 0);
    // REVIEW[FIX-UI-03] 动作状态也保留刘看山头像，使用同一份 flash 位图，不创建 GIF 画布。
    head_ = lv_image_create(root_);
    lv_image_set_src(head_, &kKanshanBwImage);
    lv_image_set_scale(head_, 128);
    lv_obj_remove_flag(head_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(head_, LV_OBJ_FLAG_CLICKABLE);
    for (auto& line : lines_) {
        line = lv_line_create(root_);
        lv_obj_set_size(line, 128, 132);
        lv_obj_set_pos(line, 0, 0);
        lv_obj_set_style_line_width(line, 3, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }
    caption_ = lv_label_create(root_);
    lv_obj_set_style_text_font(caption_, LV_FONT_DEFAULT, 0);
    lv_obj_align(caption_, LV_ALIGN_BOTTOM_MID, 0, 0);
    timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<StickmanRenderer*>(lv_timer_get_user_data(timer))->Draw();
        },
        50, this);
    if (timer_)
        lv_timer_pause(timer_);
    else
        ESP_LOGE(TAG, "LVGL timer allocation failed; poses remain static");
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Created: 128x154 vector, 20fps, fixed point storage, no GIF canvas");
}

StickmanRenderer::~StickmanRenderer() {
    if (timer_)
        lv_timer_delete(timer_);
    if (root_)
        lv_obj_delete(root_);
}

void StickmanRenderer::Show(PetAction action) {
    if (action >= PetAction::Count)
        action = PetAction::Idle;
    if (!visible_ || action != action_) {
        action_ = action;
        started_ms_ = stats_ms_ = last_frame_ms_ = lv_tick_get();
        frames_ = max_interval_ms_ = 0;
        lv_label_set_text(caption_, kNames[static_cast<unsigned>(action)]);
        lv_obj_align(caption_, LV_ALIGN_BOTTOM_MID, 0, 0);
        ESP_LOGI(TAG, "Action=%s", kNames[static_cast<unsigned>(action)]);
    }
    visible_ = true;
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (timer_ && !paused_)
        lv_timer_resume(timer_);
    Draw();
}

void StickmanRenderer::Hide() {
    visible_ = false;
    if (timer_)
        lv_timer_pause(timer_);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void StickmanRenderer::SetBackgroundColor(lv_color_t color) {
    lv_obj_set_style_bg_color(root_, color, 0);
}

void StickmanRenderer::Pause(bool paused) {
    if (paused_ == paused)
        return;
    paused_ = paused;
    if (!timer_)
        return;
    if (paused || !visible_)
        lv_timer_pause(timer_);
    else {
        last_frame_ms_ = stats_ms_ = lv_tick_get();
        frames_ = max_interval_ms_ = 0;
        lv_timer_resume(timer_);
    }
}

lv_point_precise_t StickmanRenderer::Point(float x, float y, float lean, int bounce) const {
    x += (100 - y) * lean;
    y += bounce;
    if (action_ == PetAction::UpsideDown) {
        x = 128 - x;
        y = 132 - y;
    }
    return {static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(y)};
}

void StickmanRenderer::Draw() {
    if (!visible_ || paused_)
        return;
    // 清旧姿态的固定舞台范围；更新点数组后再次失效，避免依赖动态线条边界。
    lv_obj_invalidate(root_);
    const uint32_t now = lv_tick_get();
    const uint32_t interval = now - last_frame_ms_;
    if (interval > max_interval_ms_)
        max_interval_ms_ = interval;
    last_frame_ms_ = now;
    ++frames_;
    const uint32_t elapsed = now - started_ms_;
    const int phase = (elapsed % 1000) / 50;
    const int wave = phase < 10 ? phase - 5 : 15 - phase;
    float lean = 0;
    int bounce = 0, hand_left = 78, hand_right = 78;
    switch (action_) {
        case PetAction::Shaking:
            lean = (elapsed / 100 % 2) ? 0.23f : -0.23f;
            hand_left = hand_right = 39;
            break;
        case PetAction::TiltLeft:
            lean = -0.32f;
            hand_left = 46 + wave;
            break;
        case PetAction::TiltRight:
            lean = 0.32f;
            hand_right = 46 + wave;
            break;
        case PetAction::UpsideDown:
            hand_left = hand_right = 42 + wave;
            break;
        case PetAction::Surprise:
            bounce = -3;
            hand_left = hand_right = 30 + wave;
            break;
        case PetAction::Celebrating:
            bounce = -wave;
            hand_left = hand_right = 25 + wave;
            break;
        case PetAction::Speaking:
            hand_right = 45 + wave * 2;
            bounce = wave / 3;
            break;
        case PetAction::Listening:
            lean = 0.12f;
            hand_right = 35 + wave / 2;
            break;
        case PetAction::Thinking:
        case PetAction::Setup:
            hand_left = 34 + wave / 2;
            break;
        case PetAction::Notifying:
            hand_left = 25 + wave * 2;
            break;
        default:
            bounce = wave / 3;
            break;
    }
    const auto color = lv_obj_get_style_text_color(root_, LV_PART_MAIN);
    const auto head = Point(64, 29, lean, bounce);
    // LVGL 缩放以图像中心为轴，源尺寸仍为 64x64。
    lv_obj_set_pos(head_, static_cast<int32_t>(head.x) - 32, static_cast<int32_t>(head.y) - 32);
    lv_image_set_rotation(head_, action_ == PetAction::UpsideDown ? 1800 : 0);
    const float coords[5][6] = {
        {64, 43, 64, 63, 64, 89},
        {64, 55, 43, 65, 30, static_cast<float>(hand_left)},
        {64, 55, 85, 65, 98, static_cast<float>(hand_right)},
        {64, 89, 50, 105, 42, 123},
        {64, 89, 78, 105, 86, 123},
    };
    for (int line = 0; line < 5; ++line) {
        // REVIEW[KEY-POINTS] LVGL 保存 points_ 指针；成员数组活到线对象销毁。
        for (int p = 0; p < 3; ++p) {
            points_[line][p] = Point(coords[line][p * 2], coords[line][p * 2 + 1], lean, bounce);
        }
        lv_obj_set_style_line_color(lines_[line], color, 0);
        lv_line_set_points(lines_[line], points_[line], 3);
    }
    lv_obj_invalidate(root_);
    if (now - stats_ms_ >= 10000) {
        // REVIEW[KEY-FPS] 这里只统计 Draw 调用，包含 Show 的立即绘制；不是 LCD 完成呈现的帧率。
        ESP_LOGI(TAG, "Stats: action=%s fps=%.1f interval_max_ms=%lu",
                 kNames[static_cast<unsigned>(action_)], frames_ * 1000.0f / (now - stats_ms_),
                 static_cast<unsigned long>(max_interval_ms_));
        frames_ = max_interval_ms_ = 0;
        stats_ms_ = now;
    }
}
