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
    }
    lv_display_delete(display);
    lv_deinit();
    return 0;
}
