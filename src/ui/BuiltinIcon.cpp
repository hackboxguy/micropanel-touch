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
    if (name == "display") {
        return LV_SYMBOL_IMAGE;
    }
    if (name == "brightness") {
        return LV_SYMBOL_CHARGE;
    }
    if (name == "theme") {
        return LV_SYMBOL_TINT;
    }
    if (name == "back") {
        return LV_SYMBOL_LEFT;
    }
    return nullptr;
}

}  // namespace micropanel_touch::ui
