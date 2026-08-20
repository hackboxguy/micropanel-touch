#include "ui/BuiltinIcon.h"

#include <lvgl.h>

namespace micropanel_touch::ui {

const char* builtin_icon_symbol(std::string_view name) {
    if (name == "info") {
        return LV_SYMBOL_EYE_OPEN;
    }
    if (name == "stats") {
        // Stacked bars, read as levels. The pinned Montserrat subset has no
        // chart glyph, and this is the closest thing in it to a meter - which
        // is what the screen behind it shows.
        return LV_SYMBOL_BARS;
    }
    if (name == "wifi" || name == "network") {
        return LV_SYMBOL_WIFI;
    }
    if (name == "settings" || name == "system") {
        return LV_SYMBOL_SETTINGS;
    }
    if (name == "lock") {
        // The pinned Montserrat symbol subset has no padlock. The keyboard
        // glyph names the action instead - this screen asks for a PIN - and,
        // unlike the settings glyph it used to borrow, it does not collide
        // with the other System tiles in a grid, where the icon is the thing
        // a user actually reads.
        return LV_SYMBOL_KEYBOARD;
    }
    if (name == "update") {
        return LV_SYMBOL_DOWNLOAD;
    }
    if (name == "reset") {
        // Erase-everything. Blunter than a refresh arrow, and correct: this
        // discards data rather than reloading it.
        return LV_SYMBOL_TRASH;
    }
    if (name == "calibration") {
        // A crosshair, which is literally what the calibration screen draws.
        return LV_SYMBOL_GPS;
    }
    if (name == "display") {
        return LV_SYMBOL_IMAGE;
    }
    if (name == "brightness") {
        return LV_SYMBOL_CHARGE;
    }
    if (name == "standby" || name == "power") {
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
