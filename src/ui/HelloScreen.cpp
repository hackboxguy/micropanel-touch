#include "ui/HelloScreen.h"

#include <cstdio>

namespace micropanel_touch::ui {

void HelloScreen::create() {
    lv_obj_t* const screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xe8edf2), 0);

    lv_obj_t* const title = lv_label_create(screen);
    lv_label_set_text(title, "MicroPanel Touch");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t* const detail = lv_label_create(screen);
    lv_label_set_text(detail, "Sprint 0: framebuffer + direct evdev");
    lv_obj_set_style_text_color(detail, lv_color_hex(0x8a94a0), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, 0, 48);

    counter_label_ = lv_label_create(screen);
    lv_obj_set_style_text_font(counter_label_, &lv_font_montserrat_20, 0);
    lv_obj_align(counter_label_, LV_ALIGN_CENTER, 0, -32);
    update_counter_label();

    lv_obj_t* const button = lv_button_create(screen);
    lv_obj_set_size(button, 240, 64);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 42);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x3d9bf0), 0);
    lv_obj_add_event_cb(button, increment_counter, LV_EVENT_CLICKED, this);

    lv_obj_t* const button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Tap to increment");
    lv_obj_center(button_label);
}

void HelloScreen::increment_counter(lv_event_t* event) {
    auto* const screen = static_cast<HelloScreen*>(lv_event_get_user_data(event));
    ++screen->counter_;
    screen->update_counter_label();
}

void HelloScreen::update_counter_label() {
    char text[48]{};
    std::snprintf(text, sizeof(text), "Counter: %u", counter_);
    lv_label_set_text(counter_label_, text);
}

}  // namespace micropanel_touch::ui
