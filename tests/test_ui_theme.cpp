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
    return 0;
}
