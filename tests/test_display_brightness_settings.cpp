#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/DisplayBrightnessSettings.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
                           "micropanel-touch-display-brightness-settings-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto path = directory / "display-brightness.conf";
    std::string diagnostic;

    const micropanel_touch::platform::DisplayBrightnessSettings settings{37U};
    assert(micropanel_touch::platform::display_brightness_settings_are_valid(settings));
    assert(micropanel_touch::platform::save_display_brightness_settings(path, settings, &diagnostic));
    const auto restored =
        micropanel_touch::platform::load_display_brightness_settings(path, &diagnostic);
    assert(restored.has_value());
    assert(restored->percent == 37U);

    assert(!micropanel_touch::platform::display_brightness_settings_are_valid({4U}));
    assert(!micropanel_touch::platform::display_brightness_settings_are_valid({101U}));
    {
        std::ofstream corrupt(path);
        corrupt << "version=1\npercent=101\n";
    }
    assert(!micropanel_touch::platform::load_display_brightness_settings(path, &diagnostic).has_value());
    assert(diagnostic == "display brightness settings are outside the supported range");
    std::filesystem::remove_all(directory);
    return 0;
}
