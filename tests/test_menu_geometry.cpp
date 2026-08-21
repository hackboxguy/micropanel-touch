#ifdef NDEBUG
#undef NDEBUG
#endif

// Every screen must fit its panel, in both orientations.
//
// The no-scroll property was asserted at 320x480 only, and the handover was
// explicit that this must not be read as both-orientation coverage: at 480x320
// a 2x3 grid becomes wider and shorter, and nothing had ever checked it. A
// button that has scrolled out of reach is indistinguishable from one that
// does nothing, so this is a correctness property rather than a cosmetic one -
// and the base-feature screens land on both geometries rather than being
// retrofitted onto the second one later.
//
// This walks the shipping config: the root menu, every menu it reaches, and
// every leaf screen behind those menus. It deliberately uses the shipping
// config rather than a copy with hidden entries switched on, because the
// property being asserted is about what the product actually renders.

#include "core/UiControl.h"
#include "core/UiEventQueue.h"
#include "platform/HeadlessDisplay.h"
#include "platform/NetworkInterfaceDetail.h"
#include "platform/NetworkTestService.h"
#include "platform/SyntheticKeypadInput.h"
#include "platform/SyntheticTouchInput.h"
#include "ui/StarterConfig.h"
#include "ui/StarterUi.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using micropanel_touch::core::UiControlCommand;
using micropanel_touch::core::UiControlCommandType;
using micropanel_touch::core::UiControlRequest;
using micropanel_touch::core::UiControlResponse;
using micropanel_touch::core::UiEventQueue;

std::uint64_t next_sequence = 1U;

// The stand-in server's state, held outside the lambdas so the screen reads it
// back the way it reads the real runner.
bool& iperf_running() {
    static bool running = false;
    return running;
}

// A test that does not finish on its own, so the screen can be looked at while
// it is still running - which is the only state the Stop button exists in.
std::uint64_t& unfinished_request() {
    static std::uint64_t request_id = 0U;
    return request_id;
}

// How many times the long test has actually been started. Attaching to a run
// already in progress must not start a second one.
int& speed_runs() {
    static int runs = 0;
    return runs;
}

bool& cancel_requested() {
    static bool requested = false;
    return requested;
}

UiControlResponse dispatch(UiEventQueue& event_queue, UiControlCommand command) {
    auto completion = std::make_shared<std::promise<UiControlResponse>>();
    std::future<UiControlResponse> response = completion->get_future();
    event_queue.push({next_sequence++, UiControlRequest{std::move(command), std::move(completion)}});
    for (unsigned int attempt = 0U; attempt < 50U; ++attempt) {
        lv_tick_inc(10U);
        lv_timer_handler();
        if (response.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            return response.get();
        }
    }
    assert(false && "headless UI did not complete a control request");
    return {};
}

void collect_buttons(lv_obj_t* object, std::vector<lv_obj_t*>* buttons) {
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(object, &lv_button_class)) {
        buttons->push_back(object);
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        collect_buttons(lv_obj_get_child(object, index), buttons);
    }
}

bool contains(lv_obj_t* ancestor, lv_obj_t* candidate) {
    for (lv_obj_t* walk = candidate; walk != nullptr; walk = lv_obj_get_parent(walk)) {
        if (walk == ancestor) {
            return true;
        }
    }
    return false;
}

// Whatever occupies pixels and could cover a control: free-standing labels,
// plus the scrollable views that clip them.
//
// A label inside a scrollable view is taller than the view and its coordinates
// run past it, but LVGL draws only the part inside - so comparing the label's
// own rectangle would report an overlap that no one can see. The view's
// rectangle is the honest one, and it is what must not cover a button.
void collect_free_labels(lv_obj_t* object, std::vector<lv_obj_t*>* labels) {
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(object, &lv_button_class)) {
        return;   // a button's caption belongs to it
    }
    if (lv_obj_check_type(object, &lv_label_class)) {
        if (std::string(lv_label_get_text(object)).find_first_not_of(" \n\r\t") !=
            std::string::npos) {
            labels->push_back(object);
        }
        return;
    }
    // Plain containers only. A textarea is scrollable too, but it is a control
    // in its own right - and the password field deliberately has its reveal
    // button sitting over its right edge, which is not something covering
    // anything.
    if (lv_obj_check_type(object, &lv_obj_class) &&
        lv_obj_has_flag(object, LV_OBJ_FLAG_SCROLLABLE) && object != lv_screen_active()) {
        labels->push_back(object);
        return;   // its contents are clipped to it
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        collect_free_labels(lv_obj_get_child(object, index), labels);
    }
}

std::string button_text(lv_obj_t* button) {
    const std::uint32_t children = lv_obj_get_child_count(button);
    for (std::uint32_t index = 0U; index < children; ++index) {
        lv_obj_t* const child = lv_obj_get_child(button, index);
        if (lv_obj_check_type(child, &lv_label_class)) {
            return lv_label_get_text(child);
        }
    }
    return {};
}

// Tiles render their icon and caption in one label, so a tile's text is not
// its caption alone. Recognize the reserved Back entry by its caption rather
// than by equality, or the walk follows it back out of the menu it is meant to
// be exploring.
bool is_back_tile(const std::string& title) {
    return title.find("Back") != std::string::npos;
}

// The run screen's log: the widest free-standing label on it.
lv_obj_t* find_wrapping_log(lv_obj_t* object) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_label_class) &&
        std::string(lv_label_get_text(object)).find("icmp_seq") != std::string::npos) {
        return object;
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        if (lv_obj_t* const found = find_wrapping_log(lv_obj_get_child(object, index));
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

// The scroll container the run screen's output lives in.
lv_obj_t* find_log_view(lv_obj_t* object) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_obj_class) &&
        lv_obj_has_flag(object, LV_OBJ_FLAG_SCROLLABLE) && object != lv_screen_active() &&
        lv_obj_get_child_count(object) == 1U &&
        lv_obj_check_type(lv_obj_get_child(object, 0), &lv_label_class)) {
        return object;
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        if (lv_obj_t* const found = find_log_view(lv_obj_get_child(object, index));
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

lv_obj_t* find_bar(lv_obj_t* object) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_bar_class)) {
        return object;
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        if (lv_obj_t* const found = find_bar(lv_obj_get_child(object, index));
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

lv_obj_t* find_textarea(lv_obj_t* object) {
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_textarea_class)) {
        return object;
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        if (lv_obj_t* const found = find_textarea(lv_obj_get_child(object, index));
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

lv_obj_t* find_button(lv_obj_t* object, const std::string& text) {
    std::vector<lv_obj_t*> buttons;
    collect_buttons(object, &buttons);
    for (lv_obj_t* const button : buttons) {
        if (button_text(button).find(text) != std::string::npos) {
            return button;
        }
    }
    return nullptr;
}

// Decode one UTF-8 codepoint, returning the bytes consumed. Invalid input
// consumes one byte and reports U+FFFD, so a malformed string is reported as
// unrenderable rather than silently walked off the end.
std::size_t decode_utf8(const std::string& text, std::size_t offset, std::uint32_t* codepoint) {
    const auto lead = static_cast<unsigned char>(text[offset]);
    std::size_t length = 1U;
    std::uint32_t value = lead;
    if ((lead & 0x80U) == 0U) {
        length = 1U;
        value = lead;
    } else if ((lead & 0xE0U) == 0xC0U) {
        length = 2U;
        value = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3U;
        value = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4U;
        value = lead & 0x07U;
    } else {
        *codepoint = 0xFFFDU;
        return 1U;
    }
    if (offset + length > text.size()) {
        *codepoint = 0xFFFDU;
        return 1U;
    }
    for (std::size_t index = 1U; index < length; ++index) {
        const auto continuation = static_cast<unsigned char>(text[offset + index]);
        if ((continuation & 0xC0U) != 0x80U) {
            *codepoint = 0xFFFDU;
            return 1U;
        }
        value = (value << 6U) | (continuation & 0x3FU);
    }
    *codepoint = value;
    return length;
}

// Every character the panel is asked to draw must exist in the pinned font.
//
// LVGL draws a missing glyph as a filled box, so a string the font cannot
// render is not a subtle degradation - it is a visible defect in the middle of
// a network name. The font is Montserrat with a sparse symbol range, and the
// codebase already carries the rule ("keep this ASCII-only") as a comment on
// one screen; this asks LVGL itself, on every screen, which is the only
// authority that cannot drift.
void assert_text_renders(const std::string& where, lv_obj_t* object) {
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(object, &lv_label_class)) {
        const std::string text = lv_label_get_text(object);
        const lv_font_t* const font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
        assert(font != nullptr);
        for (std::size_t offset = 0U; offset < text.size();) {
            std::uint32_t codepoint = 0U;
            offset += decode_utf8(text, offset, &codepoint);
            if (codepoint == '\n' || codepoint == '\r') {
                continue;
            }
            lv_font_glyph_dsc_t glyph{};
            if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0U)) {
                std::cerr << where << ": no glyph for U+" << std::hex << std::uppercase
                          << codepoint << std::dec << " in \"" << text << "\"\n";
                assert(false && "the pinned font cannot render this text");
            }
        }
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        assert_text_renders(where, lv_obj_get_child(object, index));
    }
}

// Text must not be drawn on top of a control.
//
// Geometry assertions say a control is inside the panel and reachable; they
// say nothing about something else occupying the same pixels. Two screens
// shipped with a wrapping label running straight over the buttons beneath it -
// the Wi-Fi summary and the Software Update blurb - and both looked correct to
// every check that existed. A label is allowed to sit *within* a control (a
// button's own caption is a child label); what is not allowed is a label that
// is not part of a control overlapping one.
// Anything that occupies pixels and is not a control's own child: a keyboard
// is the other one. A keyboard drawn over a button hides it just as
// effectively as a label does, and it is larger.
void collect_keyboards(lv_obj_t* object, std::vector<lv_obj_t*>* keyboards) {
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(object, &lv_keyboard_class)) {
        keyboards->push_back(object);
        return;
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        collect_keyboards(lv_obj_get_child(object, index), keyboards);
    }
}

// A one-line label that needs two.
//
// LVGL wraps rather than overflows, so a label given too little width does not
// look broken to a geometry check - it silently grows downward into whatever
// is beneath it. The "Address" field label did exactly that. A short string
// with no break in it, rendered taller than one line, is being squeezed.
void assert_short_labels_fit_one_line(const std::string& where, lv_obj_t* object) {
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(object, &lv_label_class) &&
        !lv_obj_check_type(lv_obj_get_parent(object), &lv_button_class)) {
        const std::string text = lv_label_get_text(object);
        const lv_font_t* const font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
        if (font != nullptr && !text.empty() && text.find('\n') == std::string::npos &&
            text.size() <= 12U && lv_label_get_long_mode(object) == LV_LABEL_LONG_WRAP) {
            const std::int32_t line_height = lv_font_get_line_height(font);
            if (lv_obj_get_height(object) > line_height + 4) {
                std::cerr << where << ": label \"" << text << "\" wrapped to "
                          << lv_obj_get_height(object) << "px (line height " << line_height
                          << ")\n";
                assert(false && "a short label was given too little width and wrapped");
            }
        }
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        assert_short_labels_fit_one_line(where, lv_obj_get_child(object, index));
    }
}

void assert_no_text_over_controls(const std::string& where, lv_obj_t* screen) {
    std::vector<lv_obj_t*> buttons;
    collect_buttons(screen, &buttons);
    std::vector<lv_obj_t*> labels;
    collect_free_labels(screen, &labels);

    std::vector<lv_obj_t*> keyboards;
    collect_keyboards(screen, &keyboards);
    for (lv_obj_t* const keyboard : keyboards) {
        lv_area_t keyboard_area{};
        lv_obj_get_coords(keyboard, &keyboard_area);
        for (lv_obj_t* const button : buttons) {
            // A keyboard's own keys are a button matrix, not buttons, so
            // anything found here belongs to the screen behind it.
            lv_area_t button_area{};
            lv_obj_get_coords(button, &button_area);
            const bool overlaps = keyboard_area.x1 <= button_area.x2 &&
                                  button_area.x1 <= keyboard_area.x2 &&
                                  keyboard_area.y1 <= button_area.y2 &&
                                  button_area.y1 <= keyboard_area.y2;
            if (overlaps) {
                std::cerr << where << ": the keyboard covers control \""
                          << button_text(button) << "\"\n";
                assert(false && "a control is underneath the keyboard");
            }
        }
    }
    for (lv_obj_t* const label : labels) {
        lv_area_t label_area{};
        lv_obj_get_coords(label, &label_area);
        for (lv_obj_t* const button : buttons) {
            lv_area_t button_area{};
            lv_obj_get_coords(button, &button_area);
            // A container holding the control is not covering it - the menu
            // grid contains its own tiles.
            if (contains(label, button)) {
                continue;
            }
            const bool overlaps = label_area.x1 <= button_area.x2 &&
                                  button_area.x1 <= label_area.x2 &&
                                  label_area.y1 <= button_area.y2 &&
                                  button_area.y1 <= label_area.y2;
            if (overlaps) {
                const char* const what = lv_obj_check_type(label, &lv_label_class)
                                             ? lv_label_get_text(label)
                                             : "<scrollable view>";
                std::cerr << where << ": " << what << " overlaps control \""
                          << button_text(button) << "\"\n";
                assert(false && "something is drawn over a control");
            }
        }
    }
}

// The two halves of "fits the panel": nothing is drawn outside it, and nothing
// is reachable only by scrolling. Either alone would pass a screen the user
// cannot fully operate.
void assert_screen_fits(const std::string& where, int width, int height) {
    lv_obj_t* const screen = lv_screen_active();
    assert_text_renders(where, screen);
    assert_no_text_over_controls(where, screen);
    assert_short_labels_fit_one_line(where, screen);
    std::vector<lv_obj_t*> buttons;
    collect_buttons(screen, &buttons);
    assert(!buttons.empty() && "a screen with no reachable control is a dead end");
    for (lv_obj_t* const button : buttons) {
        lv_area_t area{};
        lv_obj_get_coords(button, &area);
        if (area.x1 < 0 || area.y1 < 0 || area.x2 >= width || area.y2 >= height) {
            std::cerr << where << ": control \"" << button_text(button) << "\" at ("
                      << area.x1 << "," << area.y1 << ")-(" << area.x2 << "," << area.y2
                      << ") leaves the " << width << "x" << height << " panel\n";
            assert(false && "control outside the panel");
        }
    }
    if (lv_obj_get_scroll_bottom(screen) > 0 || lv_obj_get_scroll_right(screen) > 0 ||
        lv_obj_get_scroll_top(screen) > 0 || lv_obj_get_scroll_left(screen) > 0) {
        std::cerr << where << ": screen scrolls (top " << lv_obj_get_scroll_top(screen)
                  << ", bottom " << lv_obj_get_scroll_bottom(screen) << ", left "
                  << lv_obj_get_scroll_left(screen) << ", right "
                  << lv_obj_get_scroll_right(screen) << ")\n";
        assert(false && "screen needs scrolling to be fully reachable");
    }
}

UiControlResponse tap(UiEventQueue& event_queue, lv_obj_t* target) {
    assert(target != nullptr);
    lv_area_t area{};
    lv_obj_get_coords(target, &area);
    UiControlCommand command;
    command.type = UiControlCommandType::Tap;
    command.x = (area.x1 + area.x2) / 2;
    command.y = (area.y1 + area.y2) / 2;
    return dispatch(event_queue, command);
}

UiControlResponse back(UiEventQueue& event_queue) {
    UiControlCommand command;
    command.type = UiControlCommandType::Back;
    return dispatch(event_queue, command);
}

void run(const std::filesystem::path& config_path, const std::filesystem::path& theme_directory,
         unsigned int width, unsigned int height) {
    const std::string geometry = std::to_string(width) + "x" + std::to_string(height);
    micropanel_touch::platform::HeadlessDisplay display(width, height);
    std::string diagnostic;
    micropanel_touch::ui::UiTheme theme(theme_directory);
    assert(theme.activate("dark", display.display(), &diagnostic));

    const auto config = micropanel_touch::ui::StarterConfig::load(config_path, &diagnostic);
    assert(config.has_value());

    UiEventQueue event_queue;
    micropanel_touch::platform::SyntheticTouchInput synthetic_touch;
    micropanel_touch::platform::SyntheticKeypadInput synthetic_keypad;
    assert(synthetic_touch.attach(&diagnostic));
    assert(synthetic_keypad.attach(&diagnostic));

    micropanel_touch::platform::DisplayStandbySettings standby_settings{true, 60U};
    micropanel_touch::platform::DisplayBrightnessSettings brightness_settings{100U};
    micropanel_touch::platform::ScreenLockSettings screen_lock_settings;

    // Everything below the UI is a no-op stand-in on purpose: this test asks
    // only where the pixels land, so a stand-in that answers immediately keeps
    // the screens deterministic and the failure, when it comes, unambiguous.
    micropanel_touch::ui::StarterUi ui(
        *config, theme, event_queue, &synthetic_touch, &synthetic_keypad,
        [&display](std::string* capture_diagnostic) { return display.capture(capture_diagnostic); },
        [] {}, [] {}, "eth0",
        [&event_queue](std::uint64_t request_id,
                       const micropanel_touch::core::NetworkOperation&, std::string*) {
            event_queue.push({0U, micropanel_touch::core::NetworkApplyResult{request_id, true,
                                                                            "Applied."}});
            return true;
        },
        [&event_queue](std::uint64_t request_id,
                       const micropanel_touch::core::SystemUpdateOperation&, std::string*) {
            event_queue.push({0U, micropanel_touch::core::SystemUpdateResult{request_id, true,
                                                                            "Armed."}});
            return true;
        },
        [&event_queue](std::uint64_t request_id, std::string*) {
            event_queue.push({0U, micropanel_touch::core::SystemUpdateCheckResult{
                                      request_id, true, false, "00.99",
                                      "This panel is up to date (00.99)."}});
            return true;
        },
        [] { return std::string("Running slot: A\nVersion: test\nUpdate state: none"); },
        [](std::string*) { return true; }, [](std::uint64_t) { return true; }, [] {},
        [](std::uint64_t) {},
        [&theme, native_display = display.display()](const std::string& requested,
                                                     std::string* theme_diagnostic) {
            return theme.activate(requested, native_display, theme_diagnostic);
        },
        [&theme] { return theme.active_skin().name; },
        [&standby_settings] {
            return std::optional<micropanel_touch::platform::DisplayStandbySettings>(
                standby_settings);
        },
        [&standby_settings](const micropanel_touch::platform::DisplayStandbySettings& requested,
                            std::string*) {
            standby_settings = requested;
            return true;
        },
        [&brightness_settings] {
            return std::optional<micropanel_touch::platform::DisplayBrightnessSettings>(
                brightness_settings);
        },
        [](const micropanel_touch::platform::DisplayBrightnessSettings&, std::string*) {
            return true;
        },
        [&brightness_settings](
            const micropanel_touch::platform::DisplayBrightnessSettings& requested, std::string*) {
            brightness_settings = requested;
            return true;
        },
        [&screen_lock_settings] {
            return std::optional<micropanel_touch::platform::ScreenLockSettings>(
                screen_lock_settings);
        },
        [&screen_lock_settings](std::string_view pin, std::string* lock_diagnostic) {
            return micropanel_touch::platform::set_screen_lock_pin(&screen_lock_settings, pin,
                                                                   lock_diagnostic);
        },
        [&screen_lock_settings](bool enabled, std::string*) {
            if (enabled && !screen_lock_settings.configured) {
                return false;
            }
            screen_lock_settings.enabled = enabled;
            return true;
        },
        [&screen_lock_settings](std::string_view pin) {
            return micropanel_touch::platform::verify_screen_lock_pin(screen_lock_settings, pin);
        },
        [](bool) {},
        [](const std::vector<micropanel_touch::platform::TouchCalibrationSample>&, std::string*) {
            return true;
        },
        [](std::string*) { return true; },
        [](micropanel_touch::platform::TouchPoint point) { return point; },
        [&event_queue] {
            // Deliberately the widest realistic values, not neutral ones: a
            // stats table is only as wide as its longest row, and a screen
            // that fits "0%" but not "100%" fits nothing worth showing.
            micropanel_touch::ui::StarterUi::SystemServices services;
            services.system_stats = [] {
                micropanel_touch::platform::SystemStats stats;
                stats.load_average_1m = 15.75;
                stats.load_average_5m = 12.5;
                stats.load_average_15m = 9.25;
                stats.cpu_busy_percent = 100U;
                stats.cpu_temperature_c = 85.5;
                stats.memory_total_kb = 8123456U;
                stats.memory_available_kb = 12345U;
                stats.uptime_seconds = 8899665U;   // 103d 0h 1m
                return stats;
            };
            // Interfaces with the longest realistic values: a full address
            // with a prefix, and enough of them that the list has to say how
            // many it is not showing.
            services.network_interfaces = [] {
                return std::vector<std::string>{"eth0",  "wlan0", "usb0",
                                                "docker0", "tun0",  "wwan0", "lo"};
            };
            services.network_interface = [](const std::string& name) {
                micropanel_touch::platform::NetworkInterfaceDetail detail;
                detail.name = name;
                detail.mac_address = "d8:3a:dd:ff:ee:dd";
                detail.operstate = "up";
                detail.carrier = true;
                detail.mtu = 1500U;
                detail.speed_mbps = 1000U;
                detail.duplex = "full";
                detail.ipv4_addresses = {"192.168.100.200/24"};
                detail.default_route = name == "eth0";
                detail.gateway = "192.168.100.1";
                detail.rx_bytes = 987654321U;
                detail.tx_bytes = 123456789U;
                detail.rx_errors = 4294967295U;
                detail.tx_errors = 4294967295U;
                detail.rx_dropped = 4294967295U;
                detail.tx_dropped = 4294967295U;
                detail.rx_bytes_per_second = 98765432.0;
                detail.tx_bytes_per_second = 12345678.0;
                return detail;
            };
            // A test that immediately produces more output than the panel can
            // show: the run screen's log is the thing most likely to overrun
            // its space, so it is measured full rather than empty.
            services.start_network_test =
                [&event_queue](std::uint64_t request_id,
                               micropanel_touch::platform::NetworkTestService::Test test,
                               const std::string& interface_name, std::vector<std::string>,
                               std::string*) {
                    if (test == micropanel_touch::platform::NetworkTestService::Test::
                                    iperf_discover) {
                        // What the handler announces: an endpoint to dial and
                        // a name to tell two panels apart. Split across two
                        // events, because the reader delivers whatever chunks
                        // it saw and a line can arrive in halves - the second
                        // server here is cut mid-line on purpose.
                        event_queue.push({610U, micropanel_touch::core::NetworkTestOutput{
                                                    request_id,
                                                    "Looking for iperf3 servers...\n"
                                                    "SERVER 192.168.1.42 5201 bench-panel.local\n"
                                                    "SERVER 192.168.1.43 53"}});
                        event_queue.push({611U, micropanel_touch::core::NetworkTestOutput{
                                                    request_id,
                                                    "01 second-panel.local\n"
                                                    // A duplicate announcement
                                                    // of one endpoint is one
                                                    // row, not two.
                                                    "SERVER 192.168.1.42 5201 bench-panel.local\n"
                                                    "[SUCCESS] 2 server(s) found\n"}});
                        event_queue.push({612U, micropanel_touch::core::NetworkTestResult{
                                                    request_id, true, "Test finished."}});
                        return true;
                    }
                    if (test == micropanel_touch::platform::NetworkTestService::Test::speed) {
                        // Minutes of work on a slow link: it prints, reports
                        // how far it has got, and keeps going. Only a
                        // cancellation ends it.
                        unfinished_request() = request_id;
                        speed_runs() += 1;
                        event_queue.push(
                            {620U, micropanel_touch::core::NetworkTestOutput{
                                       request_id, "Downloading 100 MiB via " + interface_name +
                                                       " (run " + std::to_string(speed_runs()) +
                                                       ")\nPROGRESS 42\n"}});
                        return true;
                    }
                    std::string chatter;
                    for (int line = 0; line < 40; ++line) {
                        // A real ping line, at the width one actually is:
                        // the part worth reading is at the end of it.
                        chatter += "64 bytes from 192.168.100.200: icmp_seq=" +
                                   std::to_string(line) + " ttl=64 time=0.312 ms via " +
                                   interface_name + "\n";
                    }
                    // Progress the panel draws as a bar, and the handler's own
                    // result marker, which is the line worth reading.
                    chatter += "PROGRESS 42\n";
                    chatter += "[SUCCESS] 90.3 Mbit/s\n";
                    event_queue.push({600U, micropanel_touch::core::NetworkTestOutput{
                                                request_id, std::move(chatter)}});
                    event_queue.push({601U, micropanel_touch::core::NetworkTestResult{
                                                request_id, true,
                                                "Test finished with a long verdict line."}});
                    return true;
                };
            // The real service answers a cancellation with a terminal
            // verdict rather than falling silent; the screen's behaviour on
            // the way out depends on that.
            services.cancel_network_test = [&event_queue] {
                cancel_requested() = true;
                if (unfinished_request() != 0U) {
                    event_queue.push({621U, micropanel_touch::core::NetworkTestResult{
                                                unfinished_request(), false, "Test cancelled."}});
                    unfinished_request() = 0U;
                }
            };
            // A stand-in server whose state is what the screen must read back.
            services.iperf_server_running = [] { return iperf_running(); };
            services.start_iperf_server = [](std::uint64_t, const std::string&,
                                             const std::string&, std::string*) {
                iperf_running() = true;
                return true;
            };
            services.stop_iperf_server = [] { iperf_running() = false; };
            // Present but inert: the geometry walk taps every tile, and a
            // Power screen built without this one would render the
            // unavailable message instead of the controls being measured.
            services.request_power = [](micropanel_touch::core::PowerAction, std::string*) {
                return true;
            };
            services.about_info = [] {
                micropanel_touch::platform::AboutInfo info;
                info.version = "00.40";
                info.app_revision = "5b3d9b3263ac2cdc2299d32bd7da319d4f5334f8";
                info.panel_variant = "luckfox-ctp";
                info.slot = "A";
                info.hostname = "micropanel-touch-bench";
                info.update_state = "candidate abandoned; committed slot retained";
                info.last_check = "up-to-date (00.40)";
                return info;
            };
            return services;
        }());
    ui.start();

    UiControlCommand capture_tree;
    capture_tree.type = UiControlCommandType::CaptureTree;
    const UiControlResponse root = dispatch(event_queue, capture_tree);
    assert(root.ok);
    assert(root.screen_id == "root");
    assert_screen_fits(geometry + " root", static_cast<int>(width), static_cast<int>(height));

    // Menu titles are read off the rendered root rather than the config so a
    // renamed or newly shown tile is walked without editing this test.
    std::vector<std::string> menu_titles;
    {
        std::vector<lv_obj_t*> tiles;
        collect_buttons(lv_screen_active(), &tiles);
        for (lv_obj_t* const tile : tiles) {
            const std::string title = button_text(tile);
            if (!title.empty() && !is_back_tile(title)) {
                menu_titles.push_back(title);
            }
        }
    }
    assert(menu_titles.size() >= 3U);

    for (const std::string& menu_title : menu_titles) {
        ui.return_to_home();
        assert(dispatch(event_queue, capture_tree).ok);
        const UiControlResponse menu = tap(event_queue, find_button(lv_screen_active(), menu_title));
        assert(menu.ok);
        const std::string menu_where = geometry + " " + menu.screen_id;
        assert_screen_fits(menu_where, static_cast<int>(width), static_cast<int>(height));

        std::vector<std::string> leaf_titles;
        {
            std::vector<lv_obj_t*> tiles;
            collect_buttons(lv_screen_active(), &tiles);
            for (lv_obj_t* const tile : tiles) {
                const std::string title = button_text(tile);
                if (!title.empty() && !is_back_tile(title)) {
                    leaf_titles.push_back(title);
                }
            }
        }
        assert(!leaf_titles.empty());

        for (const std::string& leaf_title : leaf_titles) {
            ui.return_to_home();
            assert(dispatch(event_queue, capture_tree).ok);
            assert(tap(event_queue, find_button(lv_screen_active(), menu_title)).ok);
            const UiControlResponse leaf =
                tap(event_queue, find_button(lv_screen_active(), leaf_title));
            assert(leaf.ok);
            assert_screen_fits(geometry + " " + leaf.screen_id, static_cast<int>(width),
                               static_cast<int>(height));

            // One step deeper where the leaf itself offers controls that open
            // another screen: the Wi-Fi list's networks lead to the password
            // screen, which carries a keyboard and is the tightest fit on a
            // short panel.
            // The interface list leads one step deeper, and that table has to
            // fit too - it is eight rows on a short panel.
            if (leaf.screen_id == "netinfo") {
                lv_obj_update_layout(lv_screen_active());
                lv_obj_t* const interface_row = find_button(lv_screen_active(), "eth0");
                assert(interface_row != nullptr && "the interface list did not render");
                const UiControlResponse detail = tap(event_queue, interface_row);
                assert(detail.ok);
                assert(detail.screen_id == "netinfo_interface");
                assert_screen_fits(geometry + " netinfo_interface", static_cast<int>(width),
                                   static_cast<int>(height));
                assert(back(event_queue).screen_id == "netinfo");
            }
            // Testing goes two steps deeper: interface, then test, then a
            // running test with a log filling the space above Back.
            if (leaf.screen_id == "nettest") {
                lv_obj_update_layout(lv_screen_active());
                lv_obj_t* const interface_row = find_button(lv_screen_active(), "eth0");
                assert(interface_row != nullptr && "the testable interface list did not render");
                const UiControlResponse menu = tap(event_queue, interface_row);
                assert(menu.ok);
                assert(menu.screen_id == "nettest_menu");
                assert_screen_fits(geometry + " nettest_menu", static_cast<int>(width),
                                   static_cast<int>(height));

                // Ping now asks for an address first, on a screen carrying a
                // numeric keyboard - the tightest fit in the whole feature.
                const UiControlResponse target =
                    tap(event_queue, find_button(lv_screen_active(), "Ping"));
                assert(target.ok);
                assert(target.screen_id == "nettest_target");
                assert_screen_fits(geometry + " nettest_target", static_cast<int>(width),
                                   static_cast<int>(height));
                // Tapping the address field must summon the keyboard. It did
                // not: the screen had none, and the shared focus helper was
                // gated on the IP Settings screen, so a tap did nothing at all.
                {
                    std::vector<lv_obj_t*> keyboards;
                    collect_keyboards(lv_screen_active(), &keyboards);
                    assert(keyboards.size() == 1U &&
                           "the address screen has no keyboard to type on");
                    lv_obj_t* const field = find_textarea(lv_screen_active());
                    assert(field != nullptr);
                    lv_obj_send_event(field, LV_EVENT_CLICKED, nullptr);
                    for (unsigned int tick = 0U; tick < 10U; ++tick) {
                        lv_tick_inc(10U);
                        lv_timer_handler();
                    }
                    assert(!lv_obj_has_flag(keyboards.front(), LV_OBJ_FLAG_HIDDEN) &&
                           "tapping the address field did not show the keyboard");
                }

                const UiControlResponse port_target =
                    (assert(back(event_queue).screen_id == "nettest_menu"),
                     tap(event_queue, find_button(lv_screen_active(), "Port")));
                assert(port_target.screen_id == "nettest_target");
                assert_screen_fits(geometry + " nettest_target (port)", static_cast<int>(width),
                                   static_cast<int>(height));
                assert(back(event_queue).screen_id == "nettest_menu");

                // iPerf: the client's settings board, its address editor, the
                // flood warning, and the server's arming step. The client
                // board is the densest screen in the product - eight tiles
                // carrying two lines each.
                assert(tap(event_queue, find_button(lv_screen_active(), "iPerf client"))
                           .screen_id == "iperf_client");
                assert_screen_fits(geometry + " iperf_client", static_cast<int>(width),
                                   static_cast<int>(height));
                // Cycling a setting redraws the board; it must still fit with
                // UDP selected, where the rate tile carries a value instead of
                // "auto".
                assert(tap(event_queue, find_button(lv_screen_active(), "Proto")).ok);
                assert_screen_fits(geometry + " iperf_client (udp)", static_cast<int>(width),
                                   static_cast<int>(height));
                assert(tap(event_queue, find_button(lv_screen_active(), "Server")).screen_id ==
                       "nettest_target");
                assert_screen_fits(geometry + " iperf_client address",
                                   static_cast<int>(width), static_cast<int>(height));
                assert(back(event_queue).screen_id == "iperf_client");

                // With no server address set, Start asks for one rather than
                // running against nothing.
                assert(tap(event_queue, find_button(lv_screen_active(), "Start")).screen_id ==
                       "nettest_target");
                assert(back(event_queue).screen_id == "iperf_client");

                // Discovery is a list of things to tap. A screen that only
                // printed what avahi said would leave the operator retyping an
                // address the panel already knew.
                assert(tap(event_queue, find_button(lv_screen_active(), "Find")).screen_id ==
                       "iperf_discover");
                assert(dispatch(event_queue, capture_tree).ok);
                lv_obj_update_layout(lv_screen_active());
                assert_screen_fits(geometry + " iperf_discover", static_cast<int>(width),
                                   static_cast<int>(height));
                {
                    const UiControlResponse tree = dispatch(event_queue, capture_tree);
                    bool saw_count = false;
                    bool saw_second = false;
                    for (const auto& widget : tree.widgets) {
                        if (widget.text.find("Found 2 servers") != std::string::npos) {
                            saw_count = true;
                        }
                        // The endpoint split across two events must be one
                        // row reading 5301, not a row reading 53.
                        if (widget.text.find("192.168.1.43:5301") != std::string::npos) {
                            saw_second = true;
                        }
                        assert(widget.text.find("SERVER ") == std::string::npos &&
                               "the raw discovery line leaked onto the screen");
                        // ".local" is on every one of these names, so it
                        // distinguishes nothing and costs a quarter of the
                        // room the row has to say which panel this is.
                        assert(widget.text.find(".local") == std::string::npos &&
                               "the row spends its width on a suffix every name shares");
                    }
                    assert(saw_count && "discovery did not say what it found");
                    assert(saw_second && "a server announced across two reads was lost");
                }
                // Tapping a row fills in both halves of what the client would
                // otherwise have to be told by hand.
                assert(tap(event_queue, find_button(lv_screen_active(), "192.168.1.43:5301"))
                           .screen_id == "iperf_client");
                {
                    const UiControlResponse tree = dispatch(event_queue, capture_tree);
                    bool saw_server = false;
                    for (const auto& widget : tree.widgets) {
                        // A non-default port is on the tile: it is the only
                        // place it is visible, and a test dialling a port
                        // nobody mentioned is a confusing failure.
                        if (widget.text.find("192.168.1.43:5301") != std::string::npos) {
                            saw_server = true;
                        }
                    }
                    assert(saw_server && "picking a server did not reach the client");
                }
                assert_screen_fits(geometry + " iperf_client (picked)", static_cast<int>(width),
                                   static_cast<int>(height));
                // The client's own port, not the panel's listen port: picking
                // a peer announcing 5301 must not move the local server there.
                assert(tap(event_queue, find_button(lv_screen_active(), "Server")).screen_id ==
                       "nettest_target");
                assert_screen_fits(geometry + " iperf_client address (port)",
                                   static_cast<int>(width), static_cast<int>(height));
                assert(back(event_queue).screen_id == "iperf_client");
                assert(back(event_queue).screen_id == "nettest_menu");

                assert(tap(event_queue, find_button(lv_screen_active(), "iPerf server"))
                           .screen_id == "iperf_server");
                assert_screen_fits(geometry + " iperf_server", static_cast<int>(width),
                                   static_cast<int>(height));
                // One button that says what pressing it will do, and the state
                // comes from the runner rather than from what was last
                // pressed - so leaving and returning shows what is true.
                assert(find_button(lv_screen_active(), "Start server") != nullptr);
                assert(tap(event_queue, find_button(lv_screen_active(), "Start server")).ok);
                assert(find_button(lv_screen_active(), "Stop server") != nullptr);
                assert_screen_fits(geometry + " iperf_server (running)", static_cast<int>(width),
                                   static_cast<int>(height));
                assert(back(event_queue).screen_id == "nettest_menu");
                assert(tap(event_queue, find_button(lv_screen_active(), "iPerf server"))
                           .screen_id == "iperf_server");
                assert(find_button(lv_screen_active(), "Stop server") != nullptr &&
                       "the server screen forgot it was running");
                assert(tap(event_queue, find_button(lv_screen_active(), "Stop server")).ok);
                assert(find_button(lv_screen_active(), "Start server") != nullptr);
                assert(back(event_queue).screen_id == "nettest_menu");

                const UiControlResponse running =
                    tap(event_queue, find_button(lv_screen_active(), "Neighbours"));
                assert(running.ok);
                assert(running.screen_id == "nettest_run");
                assert(dispatch(event_queue, capture_tree).ok);
                lv_obj_update_layout(lv_screen_active());
                assert_screen_fits(geometry + " nettest_run", static_cast<int>(width),
                                   static_cast<int>(height));
                {
                    // The verdict is the handler's marker, not the service's
                    // "Test finished." - and neither the marker nor the
                    // PROGRESS lines belong in the log.
                    const UiControlResponse run_tree = dispatch(event_queue, capture_tree);
                    bool saw_result = false;
                    for (const auto& widget : run_tree.widgets) {
                        if (widget.text == "90.3 Mbit/s") {
                            saw_result = true;
                        }
                        assert(widget.text.find("PROGRESS 42") == std::string::npos &&
                               "a progress line leaked into the visible text");
                        assert(widget.text.find("[SUCCESS]") == std::string::npos &&
                               "the raw result marker leaked into the visible text");
                        // Newlines must survive the renderable-text pass: they
                        // are not glyphs, and substituting them turns a test's
                        // whole output into one clipped line.
                        assert(widget.text.find("ms via eth0?") == std::string::npos &&
                               "a newline was substituted as an unrenderable character");
                    }
                    assert(saw_result && "the handler's result never reached the screen");

                    // Output whose tail carries the answer must not be clipped
                    // at the right edge. A ping line is far wider than a
                    // portrait panel at the body size, so the log wraps and
                    // uses the skin's small font.
                    lv_obj_t* const log = find_wrapping_log(lv_screen_active());
                    assert(log != nullptr && "the run screen has no log");
                    assert(lv_label_get_long_mode(log) == LV_LABEL_LONG_WRAP &&
                           "the log clips instead of wrapping");
                    const lv_font_t* const log_font =
                        lv_obj_get_style_text_font(log, LV_PART_MAIN);
                    const lv_font_t* const body =
                        lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN);
                    assert(log_font != nullptr && body != nullptr);
                    assert(lv_font_get_line_height(log_font) < lv_font_get_line_height(body) &&
                           "the log is not using a smaller font than the body text");

                    // More output than the panel holds, ending at the newest
                    // line. An iperf3 run's summary is the last thing it
                    // prints, and before the log scrolled it was the part
                    // hidden behind Back: the reader had to take the answer on
                    // faith. Both halves matter - that earlier output is still
                    // reachable by swiping (scroll_top), and that the screen
                    // lands on the newest line without swiping (scroll_bottom).
                    lv_obj_t* const view = lv_obj_get_parent(log);
                    assert(view != nullptr && view != lv_screen_active() &&
                           "the log is not inside a view of its own");
                    assert((lv_obj_get_scroll_dir(view) & LV_DIR_VER) != 0 &&
                           "the log view does not scroll vertically");
                    lv_obj_update_layout(view);
                    assert(lv_obj_get_scroll_top(view) > 0 &&
                           "the fixture no longer overflows the log; this stopped "
                           "testing scrollback");
                    assert(lv_obj_get_scroll_bottom(view) == 0 &&
                           "the log is not pinned to the newest output");
                }
                assert(back(event_queue).screen_id == "nettest_menu");

                // A test long enough to want out of. Stop is there while it
                // runs, and gone once there is nothing left to stop.
                cancel_requested() = false;
                assert(tap(event_queue, find_button(lv_screen_active(), "Speed")).screen_id ==
                       "nettest_run");
                assert(dispatch(event_queue, capture_tree).ok);
                lv_obj_update_layout(lv_screen_active());
                assert_screen_fits(geometry + " nettest_run (running)", static_cast<int>(width),
                                   static_cast<int>(height));
                {
                    // One short line of output. The log pins itself to the
                    // newest text, and with less text than the view holds
                    // "newest" is still the top - a view scrolled anyway puts
                    // the only line there is out of sight below its own
                    // bottom edge.
                    lv_obj_t* const log = find_log_view(lv_screen_active());
                    assert(log != nullptr && "the running screen has no log view");
                    assert(lv_obj_get_scroll_y(log) == 0 &&
                           "the log scrolled away from output that fits");
                }
                lv_obj_t* const stop = find_button(lv_screen_active(), "Stop");
                assert(stop != nullptr && "a running test offers no way to stop it");
                assert(find_button(lv_screen_active(), "Back") != nullptr &&
                       "stopping a test replaced the way out of the screen");
                // Stepping off the screen leaves the test running: a speed
                // check is minutes of work, and walking away from the screen
                // is not a decision to abandon it.
                assert(back(event_queue).screen_id == "nettest_menu");
                // The test keeps talking while its screen is gone. This is
                // the output nobody is looking at, which is exactly the output
                // that used to be thrown away.
                event_queue.push({622U, micropanel_touch::core::NetworkTestOutput{
                                            unfinished_request(),
                                            "PROGRESS 77\nspoken to an empty room\n"}});
                assert(dispatch(event_queue, capture_tree).ok);
                assert(tap(event_queue, find_button(lv_screen_active(), "Speed")).screen_id ==
                       "nettest_run");
                assert(dispatch(event_queue, capture_tree).ok);
                lv_obj_update_layout(lv_screen_active());
                assert(speed_runs() == 1 && "re-entering restarted the test instead of joining it");
                assert(find_button(lv_screen_active(), "Stop") != nullptr &&
                       "the screen forgot the test was still running");
                {
                    // The output and the progress it reported are both still
                    // there - the bar does not begin again at zero, and the
                    // line printed while nobody was looking is not lost.
                    const UiControlResponse tree = dispatch(event_queue, capture_tree);
                    bool saw_first_run = false;
                    bool saw_away_output = false;
                    for (const auto& widget : tree.widgets) {
                        if (widget.text.find("(run 1)") != std::string::npos) {
                            saw_first_run = true;
                        }
                        if (widget.text.find("spoken to an empty room") != std::string::npos) {
                            saw_away_output = true;
                        }
                        assert(widget.text.find("(run 2)") == std::string::npos &&
                               "a second run started behind the first");
                    }
                    assert(saw_first_run && "output printed before leaving was dropped");
                    assert(saw_away_output && "output printed while away was dropped");
                    lv_obj_t* const bar = find_bar(lv_screen_active());
                    assert(bar != nullptr && "the progress bar did not come back");
                    // 77, not 42: the bar shows where the test has got to, not
                    // where it was when the screen was last looked at.
                    assert(lv_bar_get_value(bar) == 77 &&
                           "progress reported while away was lost");
                }
                // Another test cannot start on top of it, and the screen says
                // which one is in the way rather than leaving it to be hunted.
                assert(back(event_queue).screen_id == "nettest_menu");
                assert(tap(event_queue, find_button(lv_screen_active(), "Ping")).screen_id ==
                       "nettest_target");
                assert(tap(event_queue, find_button(lv_screen_active(), "Ping")).screen_id ==
                       "nettest_run");
                assert(dispatch(event_queue, capture_tree).ok);
                {
                    const UiControlResponse tree = dispatch(event_queue, capture_tree);
                    bool named_the_blocker = false;
                    for (const auto& widget : tree.widgets) {
                        if (widget.text.find("speed is running on") != std::string::npos) {
                            named_the_blocker = true;
                        }
                    }
                    assert(named_the_blocker && "the screen did not say what was in the way");
                    assert(find_button(lv_screen_active(), "Stop") == nullptr &&
                           "a screen with nothing running offered to stop something");
                }
                assert_screen_fits(geometry + " nettest_run (blocked)", static_cast<int>(width),
                                   static_cast<int>(height));
                assert(back(event_queue).screen_id == "nettest_menu");

                // Back to the running test, and stop it from its own screen.
                assert(tap(event_queue, find_button(lv_screen_active(), "Speed")).screen_id ==
                       "nettest_run");
                assert(dispatch(event_queue, capture_tree).ok);
                lv_obj_update_layout(lv_screen_active());
                assert(tap(event_queue, find_button(lv_screen_active(), "Stop")).ok);
                assert(cancel_requested() && "Stop did not stop anything");
                assert(dispatch(event_queue, capture_tree).ok);
                lv_obj_update_layout(lv_screen_active());
                assert(find_button(lv_screen_active(), "Stop") == nullptr &&
                       "the stop button outlived the test it could stop");
                {
                    // The button that stopped it becomes the one that runs it
                    // again, in the same place.
                    lv_obj_t* const again = find_button(lv_screen_active(), "Run again");
                    assert(again != nullptr && "a finished test cannot be run again");
                    assert(find_button(lv_screen_active(), "Back") != nullptr &&
                           "the way out of the screen went with the stop button");
                }
                assert_screen_fits(geometry + " nettest_run (stopped)", static_cast<int>(width),
                                   static_cast<int>(height));
                // A finished test keeps its answer: leaving and returning
                // shows the verdict rather than silently running it again.
                assert(back(event_queue).screen_id == "nettest_menu");
                assert(tap(event_queue, find_button(lv_screen_active(), "Speed")).screen_id ==
                       "nettest_run");
                assert(dispatch(event_queue, capture_tree).ok);
                lv_obj_update_layout(lv_screen_active());
                assert(speed_runs() == 1 && "returning to a finished test ran it again");
                assert(find_button(lv_screen_active(), "Run again") != nullptr);
                // And running it again is one press.
                assert(tap(event_queue, find_button(lv_screen_active(), "Run again")).screen_id ==
                       "nettest_run");
                assert(dispatch(event_queue, capture_tree).ok);
                assert(speed_runs() == 2 && "Run again did not run it again");
                assert(tap(event_queue, find_button(lv_screen_active(), "Stop")).ok);
                assert(dispatch(event_queue, capture_tree).ok);
                assert(back(event_queue).screen_id == "nettest_menu");
                assert(back(event_queue).screen_id == "nettest");
            }
            if (leaf.screen_id == "wifi") {
                // The list is empty until a scan arrives, and *entering* the
                // screen clears the previous result - so the fixture delivers
                // one every time the screen is opened, the way the worker
                // would, rather than once up front.
                auto deliver_scan = [&event_queue] {
                    event_queue.push({500U, micropanel_touch::core::WifiScanResult{
                                            {{true, "Joined AP Café — über 18",
                                              "00:11:22:33:44:55", 100U, "WPA2"},
                                             {false, "Bench AP", "00:11:22:33:44:56", 74U, "WPA2"},
                                             {false, "Open AP", "00:11:22:33:44:57", 51U, ""},
                                             {false, "Another AP", "00:11:22:33:44:58", 38U, "WPA2"},
                                             {false, "Yet Another", "00:11:22:33:44:59", 21U, "WPA2"},
                                             {false, "Sixth", "00:11:22:33:44:5a", 12U, "WPA2"},
                                             {false, "Seventh", "00:11:22:33:44:5b", 8U, "WPA2"}},
                                            {},
                                            "Joined AP Café — über 18"}});
                };
                deliver_scan();
                assert(dispatch(event_queue, capture_tree).ok);
                // The rows are created at the tail of the event drain, after
                // the capture's own settle barrier, so nothing has laid them
                // out yet and their coordinates are still zero.
                lv_obj_update_layout(lv_screen_active());
                // More networks than any panel can show: the count of rows is
                // computed from the geometry, so this is where a list that
                // overflowed a short panel would be caught.
                assert_screen_fits(geometry + " wifi (populated)", static_cast<int>(width),
                                   static_cast<int>(height));
                lv_obj_t* const network = find_button(lv_screen_active(), "Bench AP");
                assert(network != nullptr && "the scan result did not render as rows");
                const UiControlResponse password = tap(event_queue, network);
                assert(password.ok);
                assert(password.screen_id == "wifi_password");
                assert_screen_fits(geometry + " wifi_password", static_cast<int>(width),
                                   static_cast<int>(height));
                // Back from the password screen returns to the list, so the
                // walk below still leaves from where it expects to.
                assert(back(event_queue).screen_id == "wifi");

                // The already-joined network leads somewhere else entirely,
                // and that screen has to fit too.
                deliver_scan();
                assert(dispatch(event_queue, capture_tree).ok);
                lv_obj_update_layout(lv_screen_active());
                lv_obj_t* const joined = find_button(lv_screen_active(), "Joined AP");
                assert(joined != nullptr && "the active network did not render");
                const UiControlResponse connected = tap(event_queue, joined);
                assert(connected.ok);
                assert(connected.screen_id == "wifi_saved");
                assert_screen_fits(geometry + " wifi_saved", static_cast<int>(width),
                                   static_cast<int>(height));
                assert(back(event_queue).screen_id == "wifi");
            }
            // Back from a leaf lands on its own parent, not the root: a leaf
            // that cannot be left is as unusable as one that cannot be seen.
            const UiControlResponse parent = back(event_queue);
            assert(parent.ok);
            if (parent.screen_id != menu.screen_id) {
                std::cerr << geometry << " " << leaf_title << " (" << leaf.screen_id
                          << "): Back landed on \"" << parent.screen_id << "\", expected \""
                          << menu.screen_id << "\"\n";
                assert(false && "leaf Back did not return to its parent menu");
            }
        }
    }

    std::cout << "menu geometry " << geometry << ": every screen fits, nothing scrolls\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    assert(argc == 5);
    const std::filesystem::path config_path = argv[1];
    const std::filesystem::path theme_directory = argv[2];
    const auto width = static_cast<unsigned int>(std::atoi(argv[3]));
    const auto height = static_cast<unsigned int>(std::atoi(argv[4]));
    assert(width > 0U && height > 0U);

    lv_init();
    run(config_path, theme_directory, width, height);
    lv_deinit();
    return 0;
}
