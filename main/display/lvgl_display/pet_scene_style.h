#pragma once

#include <lvgl.h>

struct PetPosition {
    int x;
    int y;
};

inline PetPosition GetPetPosition(int width, int height, int font_height, bool has_scene) {
    // In the 284x240 igloo artwork the mat is at (71,173). The 128px
    // idle frame's feet are near (67,123), not at the canvas centre.
    if (has_scene)
        return {width / 2 - 74, height / 2 - 6};
    return {width / 2, height - 64 - font_height - 10};
}

inline void ApplyPetSceneTint(lv_obj_t* image, bool night) {
    if (!image)
        return;
    // EAF inherits lv_image_class: tint its displayed frame, preserving alpha.
    // 32/255 = 12.5%; no second decoded frame or filtered asset is needed.
    lv_obj_set_style_image_recolor(image, lv_color_hex(0x18385b), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(image, night ? 32 : LV_OPA_TRANSP, LV_PART_MAIN);
}
