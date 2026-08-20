#ifdef NDEBUG
#undef NDEBUG
#endif

#include "ui/BuiltinIcon.h"

#include <cassert>
#include <cstring>

#include <lvgl.h>

int main() {
    using micropanel_touch::ui::builtin_icon_symbol;

    assert(std::strcmp(builtin_icon_symbol("wifi"), LV_SYMBOL_WIFI) == 0);
    assert(std::strcmp(builtin_icon_symbol("network"), LV_SYMBOL_WIFI) == 0);
    assert(std::strcmp(builtin_icon_symbol("settings"), LV_SYMBOL_SETTINGS) == 0);
    assert(std::strcmp(builtin_icon_symbol("info"), LV_SYMBOL_EYE_OPEN) == 0);
    assert(std::strcmp(builtin_icon_symbol("stats"), LV_SYMBOL_BARS) == 0);
    assert(std::strcmp(builtin_icon_symbol("brightness"), LV_SYMBOL_CHARGE) == 0);
    assert(std::strcmp(builtin_icon_symbol("standby"), LV_SYMBOL_POWER) == 0);
    // Standby and Power share the glyph deliberately: they are the same idea
    // at two scales, and they never appear in the same menu.
    assert(std::strcmp(builtin_icon_symbol("power"), LV_SYMBOL_POWER) == 0);
    assert(std::strcmp(builtin_icon_symbol("theme"), LV_SYMBOL_TINT) == 0);
    assert(std::strcmp(builtin_icon_symbol("orientation"), LV_SYMBOL_REFRESH) == 0);
    assert(std::strcmp(builtin_icon_symbol("back"), LV_SYMBOL_LEFT) == 0);
    assert(builtin_icon_symbol("/tmp/custom-icon.png") == nullptr);
    assert(builtin_icon_symbol("unknown") == nullptr);
    return 0;
}
