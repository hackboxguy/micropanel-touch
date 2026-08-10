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
    assert(std::strcmp(builtin_icon_symbol("back"), LV_SYMBOL_LEFT) == 0);
    assert(builtin_icon_symbol("/tmp/custom-icon.png") == nullptr);
    assert(builtin_icon_symbol("unknown") == nullptr);
    return 0;
}
