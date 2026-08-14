#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

namespace micropanel_touch::platform {

// A small wrapper around the kernel-owned backlight attribute selected by a
// panel profile.  It deliberately has no GPIO fallback: a profile without a
// verified kernel interface cannot opt in to display sleep.
class SysfsBacklight {
public:
    explicit SysfsBacklight(std::filesystem::path brightness_path);

    bool set_enabled(bool enabled, std::string* diagnostic);

private:
    std::filesystem::path brightness_path_;
    int resume_brightness_{1};
};

// Coordinates the observable part of display sleep.  LVGL ownership remains
// in main.cpp through the refresh callback, keeping this class independent of
// a particular display driver and straightforward to exercise in tests.
class DisplaySleepController {
public:
    using BacklightCallback = std::function<bool(bool enabled, std::string* diagnostic)>;
    using RefreshCallback = std::function<void(bool enabled)>;

    DisplaySleepController(std::chrono::seconds timeout, BacklightCallback backlight,
                           RefreshCallback refresh);

    bool enabled() const;
    bool sleeping() const;
    void set_timeout(std::chrono::seconds timeout);

    // Lets the UI prepare the visible state immediately before the backlight
    // is blanked, without duplicating the controller's timeout policy.
    bool should_sleep(std::chrono::milliseconds inactive_time, bool action_busy) const;

    // Returns true only when a sleep or wake transition completed. Active
    // actions are an explicit wake/sleep-inhibit product rule.
    bool update(std::chrono::milliseconds inactive_time, bool action_busy,
                std::string* diagnostic);

    // Returns whether this input woke (or attempted to wake) the display. The
    // caller uses that result to consume the complete wake gesture.
    bool on_input_activity(std::string* diagnostic);

private:
    bool sleep(std::string* diagnostic);
    bool wake(std::string* diagnostic);

    std::chrono::milliseconds timeout_;
    BacklightCallback backlight_;
    RefreshCallback refresh_;
    bool sleeping_{false};
};

}  // namespace micropanel_touch::platform
