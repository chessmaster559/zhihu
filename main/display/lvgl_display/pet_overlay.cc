#include "pet_overlay.h"

namespace {
// Truncate at a UTF-8 boundary, bounding label storage even for long replies.
template <size_t N>
void CopyText(char (&out)[N], const char* text) {
    size_t used = 0;
    if (text) {
        while (text[used] && used < N - 1) {
            out[used] = text[used];
            ++used;
        }
        if (used == N - 1 && text[used]) {
            while (used && (static_cast<unsigned char>(text[used]) & 0xc0) == 0x80)
                --used;
        }
    }
    out[used] = 0;
}

lv_obj_t* PlainObject(lv_obj_t* parent, int width, int height) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, width, height);
    // The overlay must not consume taps or become a scrollable surface.
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

void Visible(lv_obj_t* obj, bool visible) {
    if (visible)
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
}  // namespace

PetOverlay::PetOverlay(lv_obj_t* screen, int width, int height, const lv_font_t* font)
    : width_(width),
      height_(height),
      pet_x_(width / 2),
      pet_y_(height - 64 - font->line_height - 10) {
    root_ = PlainObject(screen, width, height);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_text_font(root_, font, 0);

    // The balloon sits above the 128px pet, with a tail toward its head.
    tail_ = PlainObject(root_, 24, 12);
    lv_obj_add_event_cb(tail_, DrawTail, LV_EVENT_DRAW_MAIN, nullptr);
    bubble_ = PlainObject(root_, width - 32, 48);
    lv_obj_set_pos(bubble_, 16, 28);
    lv_obj_set_style_radius(bubble_, 14, 0);
    lv_obj_set_style_bg_color(bubble_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(bubble_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bubble_, lv_color_black(), 0);
    lv_obj_set_style_border_width(bubble_, 2, 0);
    speech_ = lv_label_create(bubble_);
    lv_label_set_text_static(speech_, speech_text_);
    lv_obj_set_style_text_color(speech_, lv_color_black(), 0);
    lv_obj_set_style_text_align(speech_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(speech_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(speech_, 6000, 0);
    lv_obj_set_pos(speech_, 10, 6);
    // Draw the white tail over the bottom border so its opening is continuous.
    lv_obj_move_foreground(tail_);

    bottom_ = PlainObject(root_, width, font->line_height + 8);
    user_ = lv_label_create(bottom_);
    system_ = lv_label_create(bottom_);
    lv_obj_t* bottom_labels[] = {user_, system_};
    for (auto* label : bottom_labels) {
        lv_obj_set_width(label, width - 16);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_anim_duration(label, 8000, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    }
    lv_label_set_text_static(user_, user_text_);
    lv_label_set_text_static(system_, system_text_);

    for (int i = 0; i < 3; ++i) {
        sleep_[i] = lv_label_create(root_);
        lv_label_set_text_static(sleep_[i], i == 0 ? "Z" : "z");
        lv_obj_set_style_text_color(sleep_[i], lv_color_hex(0x6085b0), 0);
        hearts_[i] = PlainObject(root_, 24, 24);
        lv_obj_add_event_cb(hearts_[i], DrawHeart, LV_EVENT_DRAW_MAIN, nullptr);
        Visible(sleep_[i], false);
        Visible(hearts_[i], false);
    }
    indicator_ = PlainObject(root_, 10, 14);
    lv_obj_set_pos(indicator_, width - 13, 32);
    lv_obj_add_event_cb(indicator_, DrawIndicator, LV_EVENT_DRAW_MAIN, this);
    Visible(indicator_, false);
    // The mat brings the head closer to the balloon. Let floating effects pass
    // behind its opaque face rather than cover the spoken text during a tap.
    lv_obj_move_foreground(bubble_);
    lv_obj_move_foreground(tail_);
    timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<PetOverlay*>(lv_timer_get_user_data(timer))->UpdateEffects();
        },
        100, this);
    lv_timer_pause(timer_);
    SetTheme(font, lv_color_black(), lv_color_white());
    UpdateVisibility();
}

PetOverlay::~PetOverlay() {
    if (timer_)
        lv_timer_delete(timer_);
    // Delete labels before their static backing buffers go out of scope.
    lv_obj_delete(root_);
}

void PetOverlay::SetTheme(const lv_font_t* font, lv_color_t foreground, lv_color_t background) {
    lv_obj_set_style_text_font(root_, font, 0);
    SetSceneColors(foreground, background, false);
    const int line_height = font->line_height;
    const int pet_top = height_ - (line_height + 8) - 128 - 2;
    const int available_lines = (pet_top - 28 - 12 - 10) / line_height;
    const int lines = available_lines < 2 ? 1 : 2;
    lv_obj_set_height(bubble_, line_height * lines + 12);
    lv_obj_set_size(speech_, width_ - 56, line_height * lines);
    lv_label_set_long_mode(speech_,
                           lines == 1 ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_SCROLL);
    lv_obj_set_pos(tail_, pet_x_ + 8, 28 + line_height * lines + 10);
    lv_obj_set_height(bottom_, line_height + 8);
    lv_obj_align(bottom_, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void PetOverlay::SetPetPosition(int center_x, int center_y) {
    pet_x_ = center_x;
    pet_y_ = center_y;
    lv_obj_set_x(tail_, pet_x_ + 8);
    UpdateEffects();
}

void PetOverlay::SetIndicator(Indicator indicator) {
    if (indicator_state_ == indicator)
        return;
    indicator_state_ = indicator;
    Visible(indicator_, indicator != Indicator::Hidden);
    lv_obj_invalidate(indicator_);
}

void PetOverlay::SetSceneColors(lv_color_t foreground, lv_color_t background, bool night) {
    lv_obj_set_style_text_color(bottom_, foreground, 0);
    lv_obj_set_style_bg_color(bottom_, background, 0);
    lv_obj_set_style_bg_opa(bottom_, LV_OPA_80, 0);
    for (auto* label : sleep_)
        lv_obj_set_style_text_color(label, lv_color_hex(night ? 0xb9d9ff : 0x6085b0), 0);
}

void PetOverlay::SetMessage(const char* role, const char* text) {
    if (!role)
        return;
    if (lv_strcmp(role, "assistant") == 0) {
        CopyText(speech_text_, text);
        lv_label_set_text_static(speech_, speech_text_);
    } else if (lv_strcmp(role, "user") == 0) {
        CopyText(user_text_, text);
        lv_label_set_text_static(user_, user_text_);
        if (user_text_[0]) {
            // A new question clears the previous answer, never the reverse.
            speech_text_[0] = system_text_[0] = 0;
            lv_label_set_text_static(speech_, speech_text_);
            lv_label_set_text_static(system_, system_text_);
        }
    } else if (lv_strcmp(role, "system") == 0) {
        CopyText(system_text_, text);
        lv_label_set_text_static(system_, system_text_);
    }
    UpdateVisibility();
}

void PetOverlay::ClearMessages() {
    speech_text_[0] = user_text_[0] = system_text_[0] = 0;
    lv_obj_t* labels[] = {speech_, user_, system_};
    for (auto* label : labels)
        lv_label_set_text_static(label, "");
    UpdateVisibility();
}

void PetOverlay::SetHidden(bool hidden) {
    hidden_ = hidden;
    UpdateVisibility();
}

void PetOverlay::UpdateVisibility() {
    const bool speech_visible = !hidden_ && speech_text_[0] && !system_text_[0];
    Visible(bubble_, speech_visible);
    Visible(tail_, speech_visible);
    Visible(bottom_, !hidden_ && (user_text_[0] || system_text_[0]));
    Visible(system_, system_text_[0]);
    Visible(user_, !system_text_[0]);
}

void PetOverlay::SetSleeping(bool sleeping) {
    if (sleeping_ == sleeping)
        return;
    sleeping_ = sleeping;
    sleep_start_ = lv_tick_get();
    UpdateEffects();
    UpdateTimer();
}

void PetOverlay::ShowHearts() {
    hearts_active_ = true;
    hearts_start_ = lv_tick_get();
    UpdateEffects();
    UpdateTimer();
}

void PetOverlay::UpdateTimer() {
    if (sleeping_ || hearts_active_)
        lv_timer_resume(timer_);
    else
        lv_timer_pause(timer_);
}

void PetOverlay::UpdateEffects() {
    // Reuse six tiny objects; no new GIF, canvas, EAF buffer or worker task.
    const uint32_t heart_elapsed = lv_tick_elaps(hearts_start_);
    if (heart_elapsed >= 1800)
        hearts_active_ = false;
    for (int i = 0; i < 3; ++i) {
        Visible(sleep_[i], sleeping_);
        if (sleeping_) {
            const uint32_t phase = (lv_tick_elaps(sleep_start_) + i * 700) % 2400;
            lv_obj_set_pos(sleep_[i], pet_x_ + 66 + i * 10,
                           pet_y_ - 31 - static_cast<int>(phase * 30 / 2400));
            lv_obj_set_style_text_opa(sleep_[i], 255 - phase * 200 / 2400, 0);
        }
        const int age = static_cast<int>(heart_elapsed) - i * 220;
        const bool show = hearts_active_ && age >= 0 && age < 1300;
        Visible(hearts_[i], show);
        if (show) {
            const int x = pet_x_ - 38 + i * 28;
            const int y = pet_y_ - 41 - age * 44 / 1300;
            lv_obj_set_pos(hearts_[i], x, y);
            lv_obj_set_style_text_opa(hearts_[i], age < 900 ? 255 : (1300 - age) * 255 / 400, 0);
            lv_obj_invalidate(hearts_[i]);
        }
    }
    UpdateTimer();
}

void PetOverlay::DrawIndicator(lv_event_t* event) {
    auto* self = static_cast<PetOverlay*>(lv_event_get_user_data(event));
    lv_area_t area;
    lv_obj_get_coords(self->indicator_, &area);
    auto* layer = lv_event_get_layer(event);
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.radius = LV_RADIUS_CIRCLE;
    const bool listening = self->indicator_state_ == Indicator::Listening;
    const bool hearing = self->indicator_state_ == Indicator::Hearing;
    // Keep a quiet but visible waiting cue; VAD brightens it without animation.
    dot.bg_opa = listening ? LV_OPA_30 : hearing ? LV_OPA_90 : LV_OPA_70;
    dot.bg_color = lv_color_hex(listening || hearing ? 0x64b87c : 0xe4bc68);
    lv_area_t bulb = {area.x1 + 1, area.y1 + 1, area.x1 + 8, area.y1 + 8};
    lv_draw_rect(layer, &dot, &bulb);
    if (self->indicator_state_ == Indicator::Thinking) {
        // A tiny vector bulb avoids a new font, emoji asset or frame buffer.
        dot.radius = 1;
        bulb = {area.x1 + 3, area.y1 + 8, area.x1 + 6, area.y1 + 10};
        lv_draw_rect(layer, &dot, &bulb);
        bulb.y1 = bulb.y2 = area.y1 + 12;
        lv_draw_rect(layer, &dot, &bulb);
    }
}

void PetOverlay::DrawTail(lv_event_t* event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target_obj(event), &area);
    auto* layer = lv_event_get_layer(event);
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = lv_color_black();
    triangle.p[0] = {area.x1, area.y1};
    triangle.p[1] = {area.x1 + 23, area.y1};
    triangle.p[2] = {area.x1 + 5, area.y1 + 11};
    lv_draw_triangle(layer, &triangle);
    triangle.color = lv_color_white();
    triangle.p[0] = {area.x1 + 3, area.y1};
    triangle.p[1] = {area.x1 + 19, area.y1};
    triangle.p[2] = {area.x1 + 6, area.y1 + 8};
    lv_draw_triangle(layer, &triangle);
}

void PetOverlay::DrawHeart(lv_event_t* event) {
    auto* obj = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    auto* layer = lv_event_get_layer(event);
    lv_draw_rect_dsc_t circle;
    lv_draw_rect_dsc_init(&circle);
    circle.bg_color = lv_color_hex(0xf05c82);
    circle.bg_opa = lv_obj_get_style_text_opa(obj, LV_PART_MAIN);
    circle.radius = LV_RADIUS_CIRCLE;
    circle.border_width = 0;
    lv_area_t lobe = {area.x1 + 1, area.y1 + 1, area.x1 + 12, area.y1 + 12};
    lv_draw_rect(layer, &circle, &lobe);
    lobe.x1 += 10;
    lobe.x2 += 10;
    lv_draw_rect(layer, &circle, &lobe);
    lv_draw_triangle_dsc_t point;
    lv_draw_triangle_dsc_init(&point);
    point.color = circle.bg_color;
    point.opa = circle.bg_opa;
    point.p[0] = {area.x1 + 1, area.y1 + 8};
    point.p[1] = {area.x1 + 22, area.y1 + 8};
    point.p[2] = {area.x1 + 12, area.y1 + 23};
    lv_draw_triangle(layer, &point);
}
