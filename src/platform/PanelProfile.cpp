#include "platform/PanelProfile.h"

#include <array>

namespace micropanel_touch::platform {
namespace {

constexpr std::array<PanelProfile, 4> kProfiles{{
    {"piscreen-ads7846-portrait", 320, 480, TouchTechnology::resistive_single_touch,
     "dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1", std::nullopt, true},
    {"piscreen-ads7846-landscape", 480, 320, TouchTechnology::resistive_single_touch,
     "dtoverlay=piscreen,drm=1,rotate=0,xohms=100,invx=1", std::nullopt, true},
    // The display and touch controller on capacitive retail panels are not a
    // universal pin-compatible device. Their exact overlay is therefore
    // deliberately left unset until it is proven on a named board; the app
    // can still select and consume its standard multitouch capability shape.
    {"piscreen-capacitive-portrait", 320, 480, TouchTechnology::capacitive_multitouch,
     std::nullopt, std::nullopt, false},
    {"piscreen-capacitive-landscape", 480, 320, TouchTechnology::capacitive_multitouch,
     std::nullopt, std::nullopt, false},
}};

}  // namespace

const std::vector<PanelProfile>& known_panel_profiles() {
    static const std::vector<PanelProfile> profiles(kProfiles.begin(), kProfiles.end());
    return profiles;
}

std::optional<PanelProfile> select_panel_profile(TouchTechnology touch_technology,
                                                  int native_width, int native_height) {
    for (const PanelProfile& profile : known_panel_profiles()) {
        if (profile.touch_technology == touch_technology &&
            profile.native_width == native_width && profile.native_height == native_height) {
            return profile;
        }
    }
    return std::nullopt;
}

}  // namespace micropanel_touch::platform
