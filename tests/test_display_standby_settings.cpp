#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/DisplayStandbySettings.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using micropanel_touch::platform::DisplayStandbySettings;

int main() {
    assert(micropanel_touch::platform::display_standby_settings_are_valid({true, 10U}));
    assert(micropanel_touch::platform::display_standby_settings_are_valid({false, 180U}));
    assert(!micropanel_touch::platform::display_standby_settings_are_valid({true, 9U}));
    assert(!micropanel_touch::platform::display_standby_settings_are_valid({true, 15U}));
    assert(!micropanel_touch::platform::display_standby_settings_are_valid({true, 190U}));

    char directory_template[] = "/tmp/micropanel-touch-standby-test-XXXXXX";
    const char* const directory = ::mkdtemp(directory_template);
    assert(directory != nullptr);
    const std::filesystem::path path = std::filesystem::path(directory) / "display-standby.conf";
    std::string diagnostic;
    const DisplayStandbySettings saved{false, 120U};
    assert(micropanel_touch::platform::save_display_standby_settings(path, saved, &diagnostic));
    const auto loaded = micropanel_touch::platform::load_display_standby_settings(path, &diagnostic);
    assert(loaded.has_value());
    assert(!loaded->enabled);
    assert(loaded->seconds == 120U);

    {
        std::ofstream corrupt(path);
        corrupt << "version=1\nenabled=1\nseconds=15\n";
    }
    assert(!micropanel_touch::platform::load_display_standby_settings(path, &diagnostic).has_value());
    assert(diagnostic == "display standby settings are outside the supported range");
    std::filesystem::remove_all(directory);
    return 0;
}
