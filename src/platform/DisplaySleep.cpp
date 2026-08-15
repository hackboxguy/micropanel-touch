#include "platform/DisplaySleep.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace micropanel_touch::platform {
namespace {

constexpr unsigned int kTransitionFailureLimit = 3U;

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

bool SysfsBacklight::read_max_brightness(int* maximum, std::string* diagnostic) const {
    if (maximum == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = "backlight maximum output is unavailable";
        }
        return false;
    }
    return parse_brightness(brightness_path_.parent_path() / "max_brightness", maximum, diagnostic);
}

bool SysfsBacklight::has_variable_brightness(std::string* diagnostic) const {
    int maximum = 0;
    return read_max_brightness(&maximum, diagnostic) && maximum > 1;
}

bool SysfsBacklight::set_brightness_percent(unsigned int percent, std::string* diagnostic) {
    if (percent < 1U || percent > 100U) {
        if (diagnostic != nullptr) {
            *diagnostic = "brightness percentage is outside the supported range";
        }
        return false;
    }
    int maximum = 0;
    if (!read_max_brightness(&maximum, diagnostic)) {
        return false;
    }
    if (maximum <= 1) {
        if (diagnostic != nullptr) {
            *diagnostic = "this panel has no variable backlight";
        }
        return false;
    }
    const int requested = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(percent) * static_cast<double>(maximum) / 100.0)),
        1, maximum);
    if (!write_brightness(brightness_path_, requested, diagnostic)) {
        return false;
    }
    resume_brightness_ = requested;
    resume_brightness_initialized_ = true;
    return true;
}

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
            resume_brightness_initialized_ = true;
        }
        return write_brightness(brightness_path_, 0, diagnostic);
    }
    int restore = std::max(resume_brightness_, 1);
    if (!resume_brightness_initialized_) {
        // A PWM panel that starts blank must never wake at raw level 1 (often
        // effectively black).  Prefer its kernel-reported maximum until a
        // real brightness choice or pre-sleep value is available.
        int maximum = 0;
        std::string ignored_diagnostic;
        if (read_max_brightness(&maximum, &ignored_diagnostic) && maximum > 1) {
            restore = maximum;
        }
    }
    return write_brightness(brightness_path_, restore, diagnostic);
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
    reset_transition_failures();
}

bool DisplaySleepController::should_sleep(std::chrono::milliseconds inactive_time,
                                          bool action_busy) const {
    return enabled() && !transition_failures_suppressed_ && !sleeping_ && !action_busy &&
           inactive_time >= timeout_;
}

bool DisplaySleepController::sleep(std::string* diagnostic) {
    if (!backlight_(false, diagnostic)) {
        return record_transition_failure(diagnostic);
    }
    refresh_(false);
    sleeping_ = true;
    reset_transition_failures();
    return true;
}

bool DisplaySleepController::wake(std::string* diagnostic) {
    if (!backlight_(true, diagnostic)) {
        return record_transition_failure(diagnostic);
    }
    refresh_(true);
    sleeping_ = false;
    reset_transition_failures();
    return true;
}

bool DisplaySleepController::update(std::chrono::milliseconds inactive_time, bool action_busy,
                                    std::string* diagnostic) {
    if (!enabled() || transition_failures_suppressed_) {
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
    // A new physical interaction is deliberate evidence that the user wants
    // the display responsive again. It is also the only automatic retry
    // trigger after the failure circuit breaker opens.
    reset_transition_failures();
    if (!sleeping_) {
        return false;
    }
    // The caller must consume this gesture even if the kernel write fails:
    // allowing it through would actuate a control with no visible feedback.
    wake(diagnostic);
    return true;
}

bool DisplaySleepController::record_transition_failure(std::string* diagnostic) {
    ++consecutive_transition_failures_;
    if (consecutive_transition_failures_ < kTransitionFailureLimit) {
        if (diagnostic != nullptr) {
            diagnostic->clear();
        }
        return false;
    }
    transition_failures_suppressed_ = true;
    if (diagnostic != nullptr && diagnostic->empty()) {
        *diagnostic = "backlight transition failed";
    }
    return false;
}

void DisplaySleepController::reset_transition_failures() {
    consecutive_transition_failures_ = 0U;
    transition_failures_suppressed_ = false;
}

}  // namespace micropanel_touch::platform
