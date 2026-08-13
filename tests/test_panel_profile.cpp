#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/PanelProfile.h"

#include <cassert>

using micropanel_touch::platform::TouchTechnology;

int main() {
    const auto resistive = micropanel_touch::platform::select_panel_profile(
        TouchTechnology::resistive_single_touch, 320, 480);
    assert(resistive.has_value());
    assert(resistive->id == "piscreen-ads7846-portrait");
    assert(resistive->boot_overlay.has_value());
    assert(*resistive->boot_overlay ==
           "dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1");
    assert(resistive->calibration_recommended);

    const auto capacitive = micropanel_touch::platform::select_panel_profile(
        TouchTechnology::capacitive_multitouch, 320, 480);
    assert(capacitive.has_value());
    assert(capacitive->id == "luckfox-ctp-st7796s-gt911-portrait");
    assert(capacitive->boot_overlay.has_value());
    assert(*capacitive->boot_overlay ==
           "dtoverlay=mipi-dbi-spi,spi0-0,speed=48000000; "
           "dtoverlay=goodix,addr=0x5d,interrupt=4,reset=17");
    assert(!capacitive->calibration_recommended);

    assert(!micropanel_touch::platform::select_panel_profile(
        TouchTechnology::capacitive_multitouch, 800, 480).has_value());
    return 0;
}
