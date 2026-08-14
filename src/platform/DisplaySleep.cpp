#include "platform/DisplaySleep.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace micropanel_touch::platform {
namespace {

bool parse_brightness(const std::filesystem::path& path, int* value, std::string* diagnostic) {
    std::ifstream input(path);
    if (!input) {
        if (diagnostic != nullptr) {
            *diagnostic = "Cannot read backlight brightness " + path.string();
        }
        return false;
    }
    std::string text;
    std::getline(input, text);
    try {
        std::size_t consumed = 0U;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size() || parsed < 0) {
            throw std::invalid_argument("invalid brightness");
        }
        *value = parsed;
        return true;
    } catch (const std::exception&) {
        if (diagnostic != nullptr) {
            *diagnostic = "Invalid backlight brightness in " + path.string();
        }
        return false;
    }
}

bool write_brightness(const std::filesystem::path& path, int value, std::string* diagnostic) {
    std::ofstream output(path);
    if (!output) {
        if (diagnostic != nullptr) {
            *diagnostic = "Cannot open backlight brightness " + path.string();
        }
        return false;
    }
    output << value << '\n';
    if (!output) {
        if (diagnostic != nullptr) {
            *diagnostic = "Cannot write backlight brightness " + path.string();
        }
        return false;
    }
    return true;
}

}  // namespace

SysfsBacklight::SysfsBacklight(std::filesystem::path brightness_path)
    : brightness_path_(std::move(brightness_path)) {}

bool SysfsBacklight::set_enabled(bool enabled, std::string* diagnostic) {
    if (!enabled) {
        int current = 0;
        if (!parse_brightness(brightness_path_, &current, diagnostic)) {
            return false;
        }
        // A panel may have booted while already blank. Preserve the last
        // usable value rather than trying to restore zero on the wake path.
        if (current > 0) {
            resume_brightness_ = current;
        }
        return write_brightness(brightness_path_, 0, diagnostic);
    }
    return write_brightness(brightness_path_, std::max(resume_brightness_, 1), diagnostic);
}

DisplaySleepController::DisplaySleepController(std::chrono::seconds timeout,
                                               BacklightCallback backlight,
                                               RefreshCallback refresh)
    : timeout_(std::chrono::duration_cast<std::chrono::milliseconds>(timeout)),
      backlight_(std::move(backlight)), refresh_(std::move(refresh)) {}

bool DisplaySleepController::enabled() const {
    return timeout_.count() > 0 && static_cast<bool>(backlight_) && static_cast<bool>(refresh_);
}

bool DisplaySleepController::sleeping() const {
    return sleeping_;
}

void DisplaySleepController::set_timeout(std::chrono::seconds timeout) {
    timeout_ = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
}

bool DisplaySleepController::should_sleep(std::chrono::milliseconds inactive_time,
                                          bool action_busy) const {
    return enabled() && !sleeping_ && !action_busy && inactive_time >= timeout_;
}

bool DisplaySleepController::sleep(std::string* diagnostic) {
    if (!backlight_(false, diagnostic)) {
        return false;
    }
    refresh_(false);
    sleeping_ = true;
    return true;
}

bool DisplaySleepController::wake(std::string* diagnostic) {
    if (!backlight_(true, diagnostic)) {
        return false;
    }
    refresh_(true);
    sleeping_ = false;
    return true;
}

bool DisplaySleepController::update(std::chrono::milliseconds inactive_time, bool action_busy,
                                    std::string* diagnostic) {
    if (!enabled()) {
        return false;
    }
    if (sleeping_) {
        return action_busy ? wake(diagnostic) : false;
    }
    if (should_sleep(inactive_time, action_busy)) {
        return sleep(diagnostic);
    }
    return false;
}

bool DisplaySleepController::on_input_activity(std::string* diagnostic) {
    if (!sleeping_) {
        return false;
    }
    // The caller must consume this gesture even if the kernel write fails:
    // allowing it through would actuate a control with no visible feedback.
    wake(diagnostic);
    return true;
}

}  // namespace micropanel_touch::platform
