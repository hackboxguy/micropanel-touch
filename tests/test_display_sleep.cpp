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
    assert(!controller.should_sleep(59999ms, false));
    assert(!controller.should_sleep(60s, true));
    assert(controller.should_sleep(60s, false));
    assert(!controller.sleeping());
    assert(controller.update(60s, false, nullptr));
    assert(controller.sleeping());
    assert(!controller.should_sleep(60s, false));
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
    controller.set_timeout(0s);
    assert(!controller.enabled());
    assert(!controller.update(60s, false, nullptr));
    controller.set_timeout(10s);
    assert(controller.enabled());
    assert(controller.update(10s, false, nullptr));

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

    const auto directory = std::filesystem::temp_directory_path() / "micropanel-touch-backlight-test";
    std::filesystem::create_directories(directory);
    const auto temporary = directory / "brightness";
    {
        std::ofstream output(temporary);
        output << "3\n";
    }
    {
        std::ofstream output(directory / "max_brightness");
        output << "15\n";
    }
    SysfsBacklight backlight(temporary);
    assert(backlight.has_variable_brightness(&diagnostic));
    assert(backlight.set_brightness_percent(50U, &diagnostic));
    {
        std::ifstream input(temporary);
        int value = -1;
        input >> value;
        assert(value == 8);
    }
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
        assert(value == 8);
    }
    {
        std::ofstream output(temporary);
        output << "0\n";
    }
    SysfsBacklight never_initialized_backlight(temporary);
    assert(never_initialized_backlight.set_enabled(true, &diagnostic));
    {
        std::ifstream input(temporary);
        int value = -1;
        input >> value;
        assert(value == 15);
    }
    std::filesystem::remove_all(directory);
    return 0;
}
