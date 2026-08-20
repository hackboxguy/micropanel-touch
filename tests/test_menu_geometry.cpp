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

// The two halves of "fits the panel": nothing is drawn outside it, and nothing
// is reachable only by scrolling. Either alone would pass a screen the user
// cannot fully operate.
void assert_screen_fits(const std::string& where, int width, int height) {
    lv_obj_t* const screen = lv_screen_active();
    assert_text_renders(where, screen);
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
        [] {
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
            if (leaf.screen_id == "wifi") {
                // The list is empty until a scan arrives, and entering the
                // screen clears the previous result - so the fixture delivers
                // one here, the way the worker would, rather than earlier.
                event_queue.push({500U, micropanel_touch::core::WifiScanResult{
                                            {{true, "Caf\u00e9 \u2014 Gast Netzwerk \u00fcber 18",
                                              "00:11:22:33:44:55", 100U, "WPA2"},
                                             {false, "Bench AP", "00:11:22:33:44:56", 74U, "WPA2"},
                                             {false, "Open AP", "00:11:22:33:44:57", 51U, ""},
                                             {false, "Another AP", "00:11:22:33:44:58", 38U, "WPA2"},
                                             {false, "Yet Another", "00:11:22:33:44:59", 21U, "WPA2"},
                                             {false, "Sixth", "00:11:22:33:44:5a", 12U, "WPA2"},
                                             {false, "Seventh", "00:11:22:33:44:5b", 8U, "WPA2"}},
                                            {}}});
                assert(dispatch(event_queue, capture_tree).ok);
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
