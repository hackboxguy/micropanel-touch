#ifdef NDEBUG
#undef NDEBUG
#endif

#include "ui/UiTheme.h"

#include <cassert>
#include <filesystem>
#include <string>

int main(int argc, char* argv[]) {
    assert(argc == 2);
    std::string diagnostic;
    const auto skin = micropanel_touch::ui::UiTheme::load_skin(argv[1], &diagnostic);
    assert(skin.has_value());
    assert(skin->name == "Dark");
    assert(skin->colors.background == 0x101418U);
    assert(skin->shape.radius == 8);
    assert(skin->shape.tile_radius == 12);
    assert(skin->fonts.body == &lv_font_montserrat_16);

    const auto missing = micropanel_touch::ui::UiTheme::load_skin(
        std::filesystem::path(argv[1]).parent_path() / "missing-theme.json", &diagnostic);
    assert(!missing.has_value());
    assert(!diagnostic.empty());

    lv_init();
    lv_display_t* const display = lv_display_create(320, 480);
    assert(display != nullptr);
    {
        micropanel_touch::ui::UiTheme theme(
            std::filesystem::path(argv[1]).parent_path());
        assert(theme.activate("dark", display, &diagnostic));

        lv_obj_t* const ordinary_button = lv_button_create(lv_screen_active());
        lv_obj_add_flag(ordinary_button, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_state(ordinary_button, LV_STATE_CHECKED);
        assert(lv_obj_get_style_radius(ordinary_button, LV_PART_MAIN) == skin->shape.radius);

        lv_obj_t* const tile_button = lv_button_create(lv_screen_active());
        theme.apply_tile_variant(tile_button);
        assert(lv_obj_get_style_radius(tile_button, LV_PART_MAIN) == skin->shape.tile_radius);

        lv_obj_t* const progress_bar = lv_bar_create(lv_screen_active());
        assert((lv_color_to_u32(lv_obj_get_style_bg_color(progress_bar, LV_PART_INDICATOR)) &
                0x00ffffffU) == skin->colors.accent);

        lv_obj_t* const slider = lv_slider_create(lv_screen_active());
        assert((lv_color_to_u32(lv_obj_get_style_bg_color(slider, LV_PART_MAIN)) & 0x00ffffffU) ==
               skin->colors.text_dim);
        assert(lv_obj_get_style_bg_opa(slider, LV_PART_MAIN) == LV_OPA_50);
        assert((lv_color_to_u32(lv_obj_get_style_bg_color(slider, LV_PART_INDICATOR)) &
                0x00ffffffU) == skin->colors.accent);
        assert((lv_color_to_u32(lv_obj_get_style_bg_color(slider, LV_PART_KNOB)) & 0x00ffffffU) ==
               skin->colors.text);

        lv_obj_t* const keyboard = lv_keyboard_create(lv_screen_active());
        assert((lv_color_to_u32(lv_obj_get_style_bg_color(keyboard, LV_PART_ITEMS)) &
                0x00ffffffU) == skin->colors.surface);
        assert((lv_color_to_u32(lv_obj_get_style_text_color(keyboard, LV_PART_ITEMS)) &
                0x00ffffffU) == skin->colors.text);
        assert(lv_obj_get_style_border_width(keyboard, LV_PART_ITEMS) ==
               skin->shape.border_width + 1);
        assert((lv_color_to_u32(lv_obj_get_style_border_color(keyboard, LV_PART_ITEMS)) &
                0x00ffffffU) == skin->colors.accent);
        assert(lv_obj_get_style_pad_row(keyboard, LV_PART_MAIN) == 3);
        assert(lv_obj_get_style_pad_column(keyboard, LV_PART_MAIN) == 3);

        lv_obj_t* const textarea = lv_textarea_create(lv_screen_active());
        assert((lv_color_to_u32(lv_obj_get_style_border_color(textarea, LV_PART_MAIN)) &
                0x00ffffffU) == skin->colors.text_dim);
        lv_obj_add_state(textarea, LV_STATE_FOCUSED);
        assert((lv_color_to_u32(lv_obj_get_style_border_color(textarea, LV_PART_MAIN)) &
                0x00ffffffU) == skin->colors.accent);
        assert(lv_obj_get_style_border_width(textarea, LV_PART_CURSOR) == 2);
        assert(lv_obj_get_style_anim_duration(textarea, LV_PART_CURSOR) == 400);
        assert((lv_color_to_u32(lv_obj_get_style_border_color(textarea, LV_PART_CURSOR)) &
                0x00ffffffU) == skin->colors.accent);

    }
    lv_display_delete(display);
    lv_deinit();
    return 0;
}
