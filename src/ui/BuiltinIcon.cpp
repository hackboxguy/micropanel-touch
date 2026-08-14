#include "ui/BuiltinIcon.h"

#include <lvgl.h>

namespace micropanel_touch::ui {

const char* builtin_icon_symbol(std::string_view name) {
    if (name == "info") {
        return LV_SYMBOL_EYE_OPEN;
    }
    if (name == "wifi" || name == "network") {
        return LV_SYMBOL_WIFI;
    }
    if (name == "settings" || name == "system") {
        return LV_SYMBOL_SETTINGS;
    }
    if (name == "lock") {
        // The pinned built-in Montserrat symbol subset has no padlock glyph.
        // Reuse the supported settings glyph rather than rendering a missing
        // character on the small panel.
        return LV_SYMBOL_SETTINGS;
    }
    if (name == "display") {
        return LV_SYMBOL_IMAGE;
    }
    if (name == "brightness") {
        return LV_SYMBOL_CHARGE;
    }
    if (name == "standby") {
        return LV_SYMBOL_POWER;
    }
    if (name == "theme") {
        return LV_SYMBOL_TINT;
    }
    if (name == "orientation") {
        return LV_SYMBOL_REFRESH;
    }
    if (name == "back") {
        return LV_SYMBOL_LEFT;
    }
    return nullptr;
}

}  // namespace micropanel_touch::ui
