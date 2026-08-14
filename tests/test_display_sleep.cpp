#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/DisplaySleep.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using micropanel_touch::platform::DisplaySleepController;
using micropanel_touch::platform::SysfsBacklight;
using namespace std::chrono_literals;

int main() {
    std::vector<bool> backlight_calls;
    std::vector<bool> refresh_calls;
    DisplaySleepController controller(
        60s,
        [&backlight_calls](bool enabled, std::string*) {
            backlight_calls.push_back(enabled);
            return true;
        },
        [&refresh_calls](bool enabled) { refresh_calls.push_back(enabled); });
    assert(controller.enabled());
    assert(!controller.update(59999ms, false, nullptr));
    assert(!controller.sleeping());
    assert(controller.update(60s, false, nullptr));
    assert(controller.sleeping());
    assert((backlight_calls == std::vector<bool>{false}));
    assert((refresh_calls == std::vector<bool>{false}));
    assert(controller.on_input_activity(nullptr));
    assert(!controller.sleeping());
    assert((backlight_calls == std::vector<bool>{false, true}));
    assert((refresh_calls == std::vector<bool>{false, true}));

    assert(controller.update(60s, false, nullptr));
    assert(controller.sleeping());
    assert(controller.update(0ms, true, nullptr));
    assert(!controller.sleeping());

    std::string diagnostic;
    DisplaySleepController failed(
        1s, [](bool, std::string* detail) {
            if (detail != nullptr) {
                *detail = "kernel rejected backlight write";
            }
            return false;
        },
        [](bool) {});
    assert(!failed.update(1s, false, &diagnostic));
    assert(!failed.sleeping());
    assert(diagnostic == "kernel rejected backlight write");

    const auto temporary = std::filesystem::temp_directory_path() / "micropanel-touch-backlight-test";
    {
        std::ofstream output(temporary);
        output << "3\n";
    }
    SysfsBacklight backlight(temporary);
    assert(backlight.set_enabled(false, &diagnostic));
    {
        std::ifstream input(temporary);
        int value = -1;
        input >> value;
        assert(value == 0);
    }
    assert(backlight.set_enabled(true, &diagnostic));
    {
        std::ifstream input(temporary);
        int value = -1;
        input >> value;
        assert(value == 3);
    }
    std::filesystem::remove(temporary);
    return 0;
}
