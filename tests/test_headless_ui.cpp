#ifdef NDEBUG
#undef NDEBUG
#endif

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
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <future>
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

UiControlResponse dispatch(UiEventQueue& event_queue, UiControlCommand command,
                           std::uint64_t sequence) {
    auto completion = std::make_shared<std::promise<UiControlResponse>>();
    std::future<UiControlResponse> response = completion->get_future();
    event_queue.push({sequence, UiControlRequest{std::move(command), std::move(completion)}});
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

lv_obj_t* find_textarea(lv_obj_t* object) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_textarea_class)) {
        return object;
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        if (lv_obj_t* const textarea = find_textarea(lv_obj_get_child(object, index));
            textarea != nullptr) {
            return textarea;
        }
    }
    return nullptr;
}

void collect_textareas(lv_obj_t* object, std::vector<lv_obj_t*>* textareas) {
    if (object == nullptr || textareas == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(object, &lv_textarea_class)) {
        textareas->push_back(object);
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        collect_textareas(lv_obj_get_child(object, index), textareas);
    }
}

// Every menu must fit its panel. A grid that overflows scrolls its tiles out
// of reach, and a button that cannot be tapped is indistinguishable from one
// that does nothing - so this is a correctness property, not a cosmetic one.
void assert_buttons_within(lv_obj_t* object, int width, int height, int& checked) {
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(object, &lv_button_class)) {
        lv_area_t area{};
        lv_obj_get_coords(object, &area);
        assert(area.x1 >= 0);
        assert(area.y1 >= 0);
        assert(area.x2 < width);
        assert(area.y2 < height);
        ++checked;
    }
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < children; ++index) {
        assert_buttons_within(lv_obj_get_child(object, index), width, height, checked);
    }
}

lv_obj_t* find_button_with_text(lv_obj_t* object, const std::string& text) {
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_button_class)) {
        const std::uint32_t children = lv_obj_get_child_count(object);
        for (std::uint32_t index = 0U; index < children; ++index) {
            lv_obj_t* const child = lv_obj_get_child(object, index);
            if (lv_obj_check_type(child, &lv_label_class) &&
                std::string(lv_label_get_text(child)).find(text) != std::string::npos) {
                return object;
            }
        }
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        if (lv_obj_t* const found = find_button_with_text(lv_obj_get_child(object, index), text);
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void collect_buttons_with_text(lv_obj_t* object, const std::string& text,
                               std::vector<lv_obj_t*>* buttons) {
    if (object == nullptr || buttons == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(object, &lv_button_class)) {
        const std::uint32_t children = lv_obj_get_child_count(object);
        for (std::uint32_t index = 0U; index < children; ++index) {
            lv_obj_t* const child = lv_obj_get_child(object, index);
            if (lv_obj_check_type(child, &lv_label_class) &&
                std::string(lv_label_get_text(child)).find(text) != std::string::npos) {
                buttons->push_back(object);
                break;
            }
        }
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        collect_buttons_with_text(lv_obj_get_child(object, index), text, buttons);
    }
}

lv_obj_t* find_dropdown(lv_obj_t* object) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_dropdown_class)) {
        return object;
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        if (lv_obj_t* const dropdown = find_dropdown(lv_obj_get_child(object, index));
            dropdown != nullptr) {
            return dropdown;
        }
    }
    return nullptr;
}

lv_obj_t* find_label_with_text(lv_obj_t* object, const std::string& text) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_label_class) && lv_label_get_text(object) == text) {
        return object;
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        if (lv_obj_t* const label = find_label_with_text(lv_obj_get_child(object, index), text);
            label != nullptr) {
            return label;
        }
    }
    return nullptr;
}

lv_obj_t* find_keyboard(lv_obj_t* object) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_keyboard_class)) {
        return object;
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        if (lv_obj_t* const keyboard = find_keyboard(lv_obj_get_child(object, index));
            keyboard != nullptr) {
            return keyboard;
        }
    }
    return nullptr;
}

lv_obj_t* find_slider(lv_obj_t* object) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_slider_class)) {
        return object;
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        if (lv_obj_t* const slider = find_slider(lv_obj_get_child(object, index)); slider != nullptr) {
            return slider;
        }
    }
    return nullptr;
}

lv_obj_t* find_checkbox(lv_obj_t* object) {
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_checkbox_class)) {
        return object;
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        if (lv_obj_t* const checkbox = find_checkbox(lv_obj_get_child(object, index));
            checkbox != nullptr) {
            return checkbox;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char* argv[]) {
    assert(argc == 3);
    const std::filesystem::path config_path = argv[1];
    const std::filesystem::path theme_directory = argv[2];

    lv_init();
    {
        micropanel_touch::platform::HeadlessDisplay display(320U, 480U);
        std::string diagnostic;
        micropanel_touch::ui::UiTheme theme(theme_directory);
        assert(theme.activate("dark", display.display(), &diagnostic));

        // The shipping config hides entries whose screens are not wired up yet.
        // This test exercises those screens' behaviour - notably the control
        // protocol's password redaction, which nothing else covers - so it runs
        // against a copy with every submenu enabled. What the *shipping* config
        // chooses to show is asserted by test_starter_config instead; keeping
        // the two concerns apart means hiding a menu entry never silently
        // deletes coverage of the screen behind it.
        // The shipping config, unmodified. The password screen used to be a
        // hidden demo entry that this test switched on; it is now reached the
        // way an operator reaches it - Wi-Fi, then a network - so the
        // redaction assertions below cover the real join path rather than a
        // screen that only the test could see.
        const auto config = micropanel_touch::ui::StarterConfig::load(config_path, &diagnostic);
        assert(config.has_value());
        UiEventQueue event_queue;
        micropanel_touch::platform::SyntheticTouchInput synthetic_touch;
        micropanel_touch::platform::SyntheticKeypadInput synthetic_keypad;
        assert(synthetic_touch.attach(&diagnostic));
        assert(synthetic_keypad.attach(&diagnostic));
        std::optional<micropanel_touch::core::NetworkOperation> network_request;
        bool hold_network_result = false;
        std::uint64_t held_network_request_id = 0U;
        std::vector<micropanel_touch::platform::TouchCalibrationSample> applied_calibration_samples;
        unsigned int calibration_reset_count = 0U;
        micropanel_touch::platform::DisplayStandbySettings display_standby_settings{true, 60U};
        unsigned int display_standby_apply_count = 0U;
        micropanel_touch::platform::DisplayBrightnessSettings display_brightness_settings{100U};
        unsigned int display_brightness_preview_percent = 100U;
        unsigned int display_brightness_preview_count = 0U;
        unsigned int display_brightness_apply_count = 0U;
        micropanel_touch::platform::ScreenLockSettings screen_lock_settings;
        bool screen_lock_session_locked = false;

        unsigned int factory_reset_requests = 0U;
        // Stand-ins for the broker: what the panel asked for, and what the
        // release server is pretending to offer.
        std::string requested_update_source;
        bool update_check_offers = true;
        std::vector<micropanel_touch::core::PowerAction> power_requests;
        bool power_available = true;
        micropanel_touch::ui::StarterUi ui(
            *config, theme, event_queue, &synthetic_touch, &synthetic_keypad,
            [&display](std::string* capture_diagnostic) { return display.capture(capture_diagnostic); },
            [] {}, [] {}, "eth0",
            [&event_queue, &network_request, &hold_network_result, &held_network_request_id](
                std::uint64_t request_id,
                const micropanel_touch::core::NetworkOperation& operation,
                std::string*) {
                network_request = operation;
                const bool is_dhcp =
                    std::holds_alternative<micropanel_touch::core::DhcpOperation>(operation);
                const bool is_dhcp_server =
                    std::holds_alternative<micropanel_touch::core::DhcpServerOperation>(operation);
                if (hold_network_result) {
                    held_network_request_id = request_id;
                } else {
                    event_queue.push({90U, micropanel_touch::core::NetworkApplyResult{
                                              request_id, true,
                                              is_dhcp ? "DHCP applied."
                                                      : (is_dhcp_server ? "DHCP server applied."
                                                                        : "Static IP applied.")}});
                }
                return true;
            },
            [&event_queue, &requested_update_source](
                std::uint64_t request_id,
                const micropanel_touch::core::SystemUpdateOperation& operation, std::string*) {
                requested_update_source = operation.source;
                event_queue.push({91U, micropanel_touch::core::SystemUpdateResult{
                                         request_id, true, "Candidate update armed."}});
                return true;
            },
            [&event_queue, &update_check_offers](std::uint64_t request_id, std::string*) {
                event_queue.push({92U, micropanel_touch::core::SystemUpdateCheckResult{
                                          request_id, true, update_check_offers, "00.99",
                                          // The wording SystemUpdateService
                                          // builds, so this reads as the
                                          // product does.
                                          update_check_offers
                                              ? "Update available: 00.99"
                                              : "This panel is up to date (00.99)."}});
                return true;
            },
            [] { return "Running slot: A\nVersion: test\nUpdate state: no candidate update recorded"; },
            [&factory_reset_requests](std::string*) {
                ++factory_reset_requests;
                return true;
            },
            [](std::uint64_t) { return true; }, [] {}, [](std::uint64_t) {},
            [&theme, native_display = display.display()](const std::string& requested,
                                                          std::string* theme_diagnostic) {
                return theme.activate(requested, native_display, theme_diagnostic);
            },
            [&theme] { return theme.active_skin().name; },
            [&display_standby_settings] {
                return std::optional<micropanel_touch::platform::DisplayStandbySettings>(
                    display_standby_settings);
            },
            [&display_standby_settings, &display_standby_apply_count](
                const micropanel_touch::platform::DisplayStandbySettings& requested,
                std::string*) {
                display_standby_settings = requested;
                ++display_standby_apply_count;
                return true;
            },
            [&display_brightness_settings] {
                return std::optional<micropanel_touch::platform::DisplayBrightnessSettings>(
                    display_brightness_settings);
            },
            [&display_brightness_preview_percent, &display_brightness_preview_count](
                const micropanel_touch::platform::DisplayBrightnessSettings& requested,
                std::string*) {
                display_brightness_preview_percent = requested.percent;
                ++display_brightness_preview_count;
                return true;
            },
            [&display_brightness_settings, &display_brightness_apply_count](
                const micropanel_touch::platform::DisplayBrightnessSettings& requested,
                std::string*) {
                display_brightness_settings = requested;
                ++display_brightness_apply_count;
                return true;
            },
            [&screen_lock_settings] {
                return std::optional<micropanel_touch::platform::ScreenLockSettings>(
                    screen_lock_settings);
            },
            [&screen_lock_settings](std::string_view pin, std::string* lock_diagnostic) {
                return micropanel_touch::platform::set_screen_lock_pin(
                    &screen_lock_settings, pin, lock_diagnostic);
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
            [&screen_lock_session_locked, &screen_lock_settings](bool locked) {
                screen_lock_session_locked = locked && screen_lock_settings.enabled;
            },
            [&applied_calibration_samples](
                const std::vector<micropanel_touch::platform::TouchCalibrationSample>& samples,
                std::string*) {
                applied_calibration_samples = samples;
                return true;
            },
            [&calibration_reset_count](std::string*) {
                ++calibration_reset_count;
                return true;
            },
            [](micropanel_touch::platform::TouchPoint point) { return point; },
            [&power_requests, &power_available] {
                micropanel_touch::ui::StarterUi::SystemServices services;
                services.request_power =
                    [&power_requests, &power_available](micropanel_touch::core::PowerAction action,
                                                        std::string* diagnostic) {
                        if (!power_available) {
                            if (diagnostic != nullptr) {
                                *diagnostic = "Restart could not be started; the panel is still running.";
                            }
                            return false;
                        }
                        power_requests.push_back(action);
                        return true;
                    };
                return services;
            }());
        ui.start();

        UiControlCommand capture_tree;
        capture_tree.type = UiControlCommandType::CaptureTree;
        const UiControlResponse root = dispatch(event_queue, capture_tree, 1U);
        assert(root.ok);
        assert(root.screen_id == "root");
        {
            int checked = 0;
            assert_buttons_within(lv_screen_active(), 320, 480, checked);
            assert(checked == 3);            // Network, Display, System
        }
        assert(std::any_of(root.widgets.begin(), root.widgets.end(), [](const auto& widget) {
            return widget.text == "MicroPanel Touch";
        }));
        assert(std::any_of(root.widgets.begin(), root.widgets.end(), [](const auto& widget) {
            return widget.text.find("Network") != std::string::npos;
        }));

        UiControlCommand back_command;
        back_command.type = UiControlCommandType::Back;
        UiControlCommand capture_frame;
        capture_frame.type = UiControlCommandType::CaptureFrame;
        const UiControlResponse root_frame = dispatch(event_queue, capture_frame, 2U);
        assert(root_frame.ok);
        assert(root_frame.frame_capture.has_value());
        assert(root_frame.frame_capture->width == 320U);
        assert(root_frame.frame_capture->height == 480U);
        assert(root_frame.frame_capture->stride_bytes == 640U);
        assert(root_frame.frame_capture->pixels.size() == 320U * 480U * 2U);
        assert(std::any_of(root_frame.frame_capture->pixels.begin(), root_frame.frame_capture->pixels.end(),
                           [](std::uint8_t byte) { return byte != 0U; }));

        UiControlCommand tap_network;
        tap_network.type = UiControlCommandType::Tap;
        tap_network.x = 80;
        tap_network.y = 120;
        const UiControlResponse network_menu = dispatch(event_queue, tap_network, 3U);
        assert(network_menu.ok);
        assert(network_menu.screen_id == "network_menu");
        {
            int checked = 0;
            assert_buttons_within(lv_screen_active(), 320, 480, checked);
            assert(checked == 4);            // Info, IP Settings, Wi-Fi, Back
        }

        // Reach the password screen the way the product does: Wi-Fi, then a
        // secured network from the scan. The scan itself is a worker in the
        // real app, so the fixture delivers its result through the same event
        // the worker would push.
        auto tap_button = [&](lv_obj_t* target, std::uint64_t id) {
            assert(target != nullptr);
            lv_area_t area{};
            lv_obj_get_coords(target, &area);
            UiControlCommand tap;
            tap.type = UiControlCommandType::Tap;
            tap.x = (area.x1 + area.x2) / 2;
            tap.y = (area.y1 + area.y2) / 2;
            return dispatch(event_queue, tap, id);
        };
        const UiControlResponse wifi_screen =
            tap_button(find_button_with_text(lv_screen_active(), "WiFi"), 4U);
        assert(wifi_screen.ok);
        assert(wifi_screen.screen_id == "wifi");

        event_queue.push({80U, micropanel_touch::core::WifiScanResult{
                                   {{false, "Bench AP", "aa:bb:cc:dd:ee:ff", 74U, "WPA2"},
                                    {false, "Open AP", "aa:bb:cc:dd:ee:00", 51U, ""}},
                                   {}}});
        assert(dispatch(event_queue, capture_tree, 5U).ok);

        // A finger stays down far longer than the 50 ms event drain, and LVGL
        // emits a click only when press and release land on the same object.
        // The rows must therefore survive a refresh that changes nothing -
        // rebuilding them unconditionally destroyed the button under the
        // finger, and the list was visible but untappable on the panel while
        // every fixture passed, because a synthetic tap is faster than the
        // refresh and a person is not.
        {
            std::vector<lv_obj_t*> rows_before;
            collect_buttons_with_text(lv_screen_active(), "Bench AP", &rows_before);
            assert(rows_before.size() == 1U);
            lv_area_t area_before{};
            lv_obj_get_coords(rows_before.front(), &area_before);

            // Twenty drain intervals: far longer than any press.
            for (unsigned int tick = 0U; tick < 100U; ++tick) {
                lv_tick_inc(10U);
                lv_timer_handler();
            }

            std::vector<lv_obj_t*> rows_after;
            collect_buttons_with_text(lv_screen_active(), "Bench AP", &rows_after);
            assert(rows_after.size() == 1U);
            assert(rows_after.front() == rows_before.front());
            lv_area_t area_after{};
            lv_obj_get_coords(rows_after.front(), &area_after);
            assert(area_after.x1 == area_before.x1 && area_after.y1 == area_before.y1);
        }

        const UiControlResponse password_screen =
            tap_button(find_button_with_text(lv_screen_active(), "Bench AP"), 6U);
        assert(password_screen.ok);
        assert(password_screen.screen_id == "wifi_password");
        // The network being joined is named on the screen: the operator picked
        // it out of a list of similar names one screen ago.
        const UiControlResponse joining_tree = dispatch(event_queue, capture_tree, 7U);
        assert(std::any_of(joining_tree.widgets.begin(), joining_tree.widgets.end(),
                           [](const auto& widget) { return widget.text == "Joining Bench AP"; }));

        lv_obj_t* const password_input = find_textarea(lv_screen_active());
        assert(password_input != nullptr);
        assert(synthetic_keypad.focus(password_input, &diagnostic));
        // Long enough to be a real WPA passphrase: the join path validates the
        // length before it sends anything, so a seven-character fixture would
        // exercise the refusal rather than the submission.
        constexpr char kSecret[] = "otter42-hunter2";
        assert(synthetic_keypad.type(kSecret, &diagnostic));
        lv_obj_update_layout(lv_screen_active());
        lv_refr_now(display.display());

        UiControlCommand forbidden_text;
        forbidden_text.type = UiControlCommandType::Text;
        forbidden_text.target = "ip_address";
        forbidden_text.text = kSecret;
        const UiControlResponse rejected = dispatch(event_queue, forbidden_text, 9U);
        assert(!rejected.ok);
        assert(rejected.error == "text injection is forbidden for password fields");

        const UiControlResponse password_tree = dispatch(event_queue, capture_tree, 10U);
        assert(password_tree.ok);
        assert(password_tree.screen_id == "wifi_password");
        bool found_redacted_textarea = false;
        for (const auto& widget : password_tree.widgets) {
            assert(widget.text.find(kSecret) == std::string::npos);
            if (widget.type == "textarea") {
                found_redacted_textarea = true;
                assert(widget.redacted);
                assert(widget.text == "<redacted>");
            }
        }
        assert(found_redacted_textarea);

        // Submitting hands the secret to the broker seam once, and nothing
        // else ever sees it. The keyboard's accept key is the submit control,
        // as it has been since this screen was a capability demo.
        {
            lv_obj_t* const keyboard = find_keyboard(lv_screen_active());
            assert(keyboard != nullptr);
            lv_obj_send_event(keyboard, LV_EVENT_READY, nullptr);
            for (unsigned int attempt = 0U; attempt < 10U; ++attempt) {
                lv_tick_inc(10U);
                lv_timer_handler();
            }
        }
        assert(network_request.has_value());
        const auto* join_request =
            std::get_if<micropanel_touch::core::WifiJoinOperation>(&*network_request);
        assert(join_request != nullptr);
        assert(join_request->ssid == "Bench AP");
        assert(join_request->passphrase == kSecret);
        // The result card names the network and never the password.
        const UiControlResponse joining = dispatch(event_queue, capture_tree, 11U);
        assert(joining.ok);
        for (const auto& widget : joining.widgets) {
            assert(widget.text.find(kSecret) == std::string::npos);
        }
        // The result card is the shared network-apply surface, which is the
        // point: joining a hotspot did not need a second result path.
        assert(joining.screen_id == "network_result");
        network_request.reset();

        // Back out of the result card, then repeat the walk to leave the
        // password screen the other way: by pressing Back on it.
        UiControlCommand back;
        back.type = UiControlCommandType::Back;
        assert(dispatch(event_queue, back, 12U).ok);
        assert(tap_button(find_button_with_text(lv_screen_active(), "WiFi"), 13U).screen_id ==
               "wifi");
        event_queue.push({81U, micropanel_touch::core::WifiScanResult{
                                   {{false, "Bench AP", "aa:bb:cc:dd:ee:ff", 74U, "WPA2"}}, {}}});
        assert(dispatch(event_queue, capture_tree, 14U).ok);
        assert(tap_button(find_button_with_text(lv_screen_active(), "Bench AP"), 15U).screen_id ==
               "wifi_password");
        // Back from the password screen returns to the list it was reached
        // from - "wrong network" is the common reason to press it - and the
        // secret does not survive the trip.
        const UiControlResponse wifi_again = dispatch(event_queue, back, 16U);
        assert(wifi_again.ok);
        assert(wifi_again.screen_id == "wifi");
        for (const auto& widget : wifi_again.widgets) {
            assert(widget.text.find(kSecret) == std::string::npos);
        }
        // --- the network already joined ---------------------------------
        // It sorts to the top whatever order the scan reported, and tapping it
        // offers a way off rather than the password keyboard for a password
        // the panel already has.
        event_queue.push({82U, micropanel_touch::core::WifiScanResult{
                                   {{false, "Bench AP", "aa:bb:cc:dd:ee:ff", 90U, "WPA2"},
                                    {false, "Open AP", "aa:bb:cc:dd:ee:00", 80U, ""},
                                    {true, "Joined AP", "aa:bb:cc:dd:ee:11", 20U, "WPA2"}},
                                   {}}});
        const UiControlResponse sorted = dispatch(event_queue, capture_tree, 18U);
        assert(sorted.ok);
        {
            // The rows are created at the tail of the event drain, so nothing
            // has laid them out yet and their coordinates are still zero.
            lv_obj_update_layout(lv_screen_active());
            // Top of the list by position, despite being weakest by signal.
            std::vector<lv_obj_t*> joined;
            collect_buttons_with_text(lv_screen_active(), "Joined AP", &joined);
            assert(joined.size() == 1U);
            std::vector<lv_obj_t*> strongest;
            collect_buttons_with_text(lv_screen_active(), "Bench AP", &strongest);
            assert(strongest.size() == 1U);
            lv_area_t joined_area{};
            lv_area_t strongest_area{};
            lv_obj_get_coords(joined.front(), &joined_area);
            lv_obj_get_coords(strongest.front(), &strongest_area);
            assert(joined_area.y1 < strongest_area.y1);
            // Carrying the skin's "ok" colour, not the default button fill.
            const lv_color_t highlight =
                lv_obj_get_style_bg_color(joined.front(), LV_PART_MAIN);
            const lv_color_t plain =
                lv_obj_get_style_bg_color(strongest.front(), LV_PART_MAIN);
            assert(lv_color_to_u32(highlight) != lv_color_to_u32(plain));
        }
        const UiControlResponse connected =
            tap_button(find_button_with_text(lv_screen_active(), "Joined AP"), 19U);
        assert(connected.ok);
        assert(connected.screen_id == "wifi_connected");
        const UiControlResponse connected_tree = dispatch(event_queue, capture_tree, 20U);
        assert(std::any_of(connected_tree.widgets.begin(), connected_tree.widgets.end(),
                           [](const auto& widget) {
                               return widget.text == "Connected to Joined AP";
                           }));
        assert(find_button_with_text(lv_screen_active(), "Disconnect") != nullptr);
        // Not the keyboard screen, and no field to type a known password into.
        assert(find_textarea(lv_screen_active()) == nullptr);

        network_request.reset();
        assert(tap_button(find_button_with_text(lv_screen_active(), "Disconnect"), 21U).ok);
        assert(network_request.has_value());
        assert(std::holds_alternative<micropanel_touch::core::WifiForgetOperation>(
            *network_request));
        network_request.reset();

        // Back from the result card leaves the Wi-Fi leaf outright - the row
        // tap deliberately did not deepen the history behind it.
        const UiControlResponse network_again = dispatch(event_queue, back, 22U);
        assert(network_again.ok);
        assert(network_again.screen_id == "network_menu");

        lv_obj_t* const ip_settings_button =
            find_button_with_text(lv_screen_active(), "IP Settings");
        assert(ip_settings_button != nullptr);
        lv_area_t ip_settings_area{};
        lv_obj_get_coords(ip_settings_button, &ip_settings_area);
        UiControlCommand tap_ip_settings;
        tap_ip_settings.type = UiControlCommandType::Tap;
        tap_ip_settings.x = (ip_settings_area.x1 + ip_settings_area.x2) / 2;
        tap_ip_settings.y = (ip_settings_area.y1 + ip_settings_area.y2) / 2;
        const UiControlResponse ip_settings = dispatch(event_queue, tap_ip_settings, 8U);
        assert(ip_settings.ok);
        assert(ip_settings.screen_id == "netsettings");
        const UiControlResponse dhcp_tree = dispatch(event_queue, capture_tree, 9U);
        assert(dhcp_tree.ok);
        assert(std::none_of(dhcp_tree.widgets.begin(), dhcp_tree.widgets.end(), [](const auto& widget) {
            return widget.type == "textarea";
        }));
        lv_obj_t* const mode_dropdown = find_dropdown(lv_screen_active());
        assert(mode_dropdown != nullptr);
        lv_obj_t* const mode_list = lv_dropdown_get_list(mode_dropdown);
        assert(mode_list != nullptr);
        // The selected-row border used to combine with our custom divider,
        // yielding a double separator on the physical display.
        assert(lv_obj_get_style_border_width(mode_list,
                                             static_cast<lv_part_t>(
                                                 LV_PART_SELECTED | LV_STATE_CHECKED)) == 0);
        lv_dropdown_set_selected(mode_dropdown, 1U);
        lv_obj_send_event(mode_dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
        const UiControlResponse static_tree = dispatch(event_queue, capture_tree, 10U);
        assert(static_tree.ok);
        for (const char* const label : {"IP address", "Gateway", "Netmask"}) {
            assert(std::any_of(static_tree.widgets.begin(), static_tree.widgets.end(), [label](const auto& widget) {
                return widget.text == label;
            }));
        }
        std::vector<lv_obj_t*> static_inputs;
        collect_textareas(lv_screen_active(), &static_inputs);
        assert(static_inputs.size() == 3U);
        assert(std::string(lv_textarea_get_text(static_inputs[0])) == "192.168.1.1");
        assert(std::string(lv_textarea_get_text(static_inputs[1])) == "192.168.1.1");
        assert(std::string(lv_textarea_get_text(static_inputs[2])) == "255.255.255.0");
        const lv_coord_t field_x = 118;
        const lv_coord_t field_width = lv_obj_get_width(static_inputs.front());
        for (lv_obj_t* const input : static_inputs) {
            lv_area_t field_area{};
            lv_obj_get_coords(input, &field_area);
            assert(field_area.x1 == field_x);
            assert(lv_obj_get_width(input) == field_width);
        }
        lv_obj_t* const static_apply_button =
            find_button_with_text(lv_screen_active(), "Apply settings");
        lv_obj_t* const static_back_button = find_button_with_text(lv_screen_active(), "Back");
        lv_obj_t* const static_keyboard = find_keyboard(lv_screen_active());
        assert(static_apply_button != nullptr);
        assert(static_back_button != nullptr);
        assert(static_keyboard != nullptr);
        lv_area_t netmask_area{};
        lv_area_t static_apply_area{};
        lv_area_t static_back_area{};
        lv_area_t static_keyboard_area{};
        lv_obj_get_coords(static_inputs[2], &netmask_area);
        lv_obj_get_coords(static_apply_button, &static_apply_area);
        lv_obj_get_coords(static_back_button, &static_back_area);
        lv_obj_get_coords(static_keyboard, &static_keyboard_area);
        assert(static_apply_area.y1 - netmask_area.y2 - 1 == 8);
        assert(static_back_area.y1 - static_apply_area.y2 - 1 == 4);
        assert(static_keyboard_area.y1 - static_back_area.y2 - 1 == 6);
        assert(lv_obj_get_height(static_keyboard) == 160);
        assert(lv_obj_get_scroll_x(lv_screen_active()) == 0);
        for (lv_obj_t* const input : static_inputs) {
            lv_textarea_set_text(input, "");
        }

        UiControlCommand enter_ip;
        enter_ip.type = UiControlCommandType::Text;
        enter_ip.target = "ip_address";
        enter_ip.text = "192.168.1.20";
        assert(dispatch(event_queue, enter_ip, 10U).ok);

        UiControlCommand focus_prefix;
        focus_prefix.type = UiControlCommandType::Tap;
        focus_prefix.x = 160;
        focus_prefix.y = 160;
        assert(dispatch(event_queue, focus_prefix, 11U).ok);
        UiControlCommand enter_prefix;
        enter_prefix.type = UiControlCommandType::Text;
        enter_prefix.target = "gateway";
        enter_prefix.text = "192.168.1.1";
        assert(dispatch(event_queue, enter_prefix, 12U).ok);

        UiControlCommand focus_gateway;
        focus_gateway.type = UiControlCommandType::Tap;
        focus_gateway.x = 160;
        focus_gateway.y = 202;
        assert(dispatch(event_queue, focus_gateway, 13U).ok);
        UiControlCommand enter_gateway;
        enter_gateway.type = UiControlCommandType::Text;
        enter_gateway.target = "netmask";
        enter_gateway.text = "255.255.255.0";
        assert(dispatch(event_queue, enter_gateway, 14U).ok);

        UiControlCommand apply;
        apply.type = UiControlCommandType::Tap;
        apply.x = 160;
        apply.y = 252;
        const UiControlResponse static_ip_result = dispatch(event_queue, apply, 15U);
        assert(static_ip_result.ok);
        assert(static_ip_result.screen_id == "network_result");
        assert(network_request.has_value());
        const auto* static_ip_request =
            std::get_if<micropanel_touch::core::StaticIpv4Operation>(&*network_request);
        assert(static_ip_request != nullptr);
        assert(static_ip_request->interface_name == "eth0");
        assert(static_ip_request->settings.address == "192.168.1.20");
        assert(static_ip_request->settings.prefix_length == "24");
        assert(static_ip_request->settings.gateway == "192.168.1.1");
        const UiControlResponse result_tree = dispatch(event_queue, capture_tree, 16U);
        assert(result_tree.ok);
        assert(std::any_of(result_tree.widgets.begin(), result_tree.widgets.end(), [](const auto& widget) {
            return widget.text == "Static IP applied.";
        }));

        const UiControlResponse network_after_static = dispatch(event_queue, back, 17U);
        assert(network_after_static.ok);
        assert(network_after_static.screen_id == "network_menu");
        const UiControlResponse dhcp_settings = dispatch(event_queue, tap_ip_settings, 18U);
        assert(dhcp_settings.ok);
        assert(dhcp_settings.screen_id == "netsettings");
        event_queue.push_latest({91U, micropanel_touch::core::ManagedIpv4Profile{
                                        "eth0", "manual", "192.168.1.20/24", "", false, "", ""}});
        const UiControlResponse restored_static_tree = dispatch(event_queue, capture_tree, 19U);
        assert(restored_static_tree.ok);
        assert(lv_dropdown_get_selected(find_dropdown(lv_screen_active())) == 1U);
        std::vector<lv_obj_t*> restored_static_inputs;
        collect_textareas(lv_screen_active(), &restored_static_inputs);
        assert(restored_static_inputs.size() == 3U);
        assert(std::string(lv_textarea_get_text(restored_static_inputs[0])) == "192.168.1.20");
        assert(std::string(lv_textarea_get_text(restored_static_inputs[1])).empty());
        assert(std::string(lv_textarea_get_text(restored_static_inputs[2])) == "255.255.255.0");
        lv_dropdown_set_selected(find_dropdown(lv_screen_active()), 0U);
        lv_obj_send_event(find_dropdown(lv_screen_active()), LV_EVENT_VALUE_CHANGED, nullptr);
        hold_network_result = true;
        UiControlCommand apply_dhcp;
        apply_dhcp.type = UiControlCommandType::Tap;
        apply_dhcp.x = 160;
        apply_dhcp.y = 388;
        const UiControlResponse dhcp_result = dispatch(event_queue, apply_dhcp, 20U);
        assert(dhcp_result.ok);
        assert(dhcp_result.screen_id == "network_result");
        assert(network_request.has_value());
        const auto* dhcp_request =
            std::get_if<micropanel_touch::core::DhcpOperation>(&*network_request);
        assert(dhcp_request != nullptr);
        assert(dhcp_request->interface_name == "eth0");
        assert(held_network_request_id != 0U);
        assert(ui.inhibits_display_sleep());
        event_queue.push({91U, micropanel_touch::core::NetworkApplyResult{
                                held_network_request_id, true, "DHCP applied."}});
        hold_network_result = false;
        const UiControlResponse dhcp_result_tree = dispatch(event_queue, capture_tree, 21U);
        assert(dhcp_result_tree.ok);
        assert(!ui.inhibits_display_sleep());
        assert(std::any_of(dhcp_result_tree.widgets.begin(), dhcp_result_tree.widgets.end(),
                           [](const auto& widget) { return widget.text == "DHCP applied."; }));

        const UiControlResponse network_after_dhcp = dispatch(event_queue, back, 22U);
        assert(network_after_dhcp.ok);
        assert(network_after_dhcp.screen_id == "network_menu");
        const UiControlResponse server_settings = dispatch(event_queue, tap_ip_settings, 23U);
        assert(server_settings.ok);
        assert(server_settings.screen_id == "netsettings");
        lv_obj_t* const server_dropdown = find_dropdown(lv_screen_active());
        assert(server_dropdown != nullptr);
        lv_dropdown_set_selected(server_dropdown, 2U);
        lv_obj_send_event(server_dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
        const UiControlResponse server_tree = dispatch(event_queue, capture_tree, 24U);
        assert(server_tree.ok);
        for (const char* const label : {"Server IP", "Lease start", "Netmask", "Lease end"}) {
            assert(std::any_of(server_tree.widgets.begin(), server_tree.widgets.end(), [label](const auto& widget) {
                return widget.text == label;
            }));
        }
        std::vector<lv_obj_t*> server_inputs;
        collect_textareas(lv_screen_active(), &server_inputs);
        assert(server_inputs.size() == 4U);
        assert(std::string(lv_textarea_get_text(server_inputs[0])) == "192.168.50.1");
        assert(std::string(lv_textarea_get_text(server_inputs[1])) == "192.168.50.100");
        assert(std::string(lv_textarea_get_text(server_inputs[2])) == "255.255.255.0");
        assert(std::string(lv_textarea_get_text(server_inputs[3])) == "192.168.50.200");
        lv_obj_t* const lease_start_label = find_label_with_text(lv_screen_active(), "Lease start");
        lv_obj_t* const lease_end_label = find_label_with_text(lv_screen_active(), "Lease end");
        assert(lease_start_label != nullptr);
        assert(lease_end_label != nullptr);
        assert(lv_obj_get_height(lease_start_label) <= 20);
        assert(lv_obj_get_height(lease_end_label) <= 20);
        lv_textarea_set_text(server_inputs[1], "");
        UiControlCommand focus_lease_start;
        focus_lease_start.type = UiControlCommandType::Tap;
        focus_lease_start.x = 160;
        focus_lease_start.y = 164;
        assert(dispatch(event_queue, focus_lease_start, 24U).ok);
        UiControlCommand enter_lease_start;
        enter_lease_start.type = UiControlCommandType::Text;
        enter_lease_start.target = "lease_start";
        enter_lease_start.text = "192.168.50.101";
        assert(dispatch(event_queue, enter_lease_start, 24U).ok);
        assert(std::string(lv_textarea_get_text(server_inputs[1])) == "192.168.50.101");
        lv_obj_t* const enable_server_button =
            find_button_with_text(lv_screen_active(), "Enable DHCP server");
        lv_obj_t* const server_back_button = find_button_with_text(lv_screen_active(), "Back");
        assert(enable_server_button != nullptr);
        assert(server_back_button != nullptr);
        assert(lv_obj_get_height(enable_server_button) == 36);
        lv_area_t enable_area{};
        lv_area_t back_area{};
        lv_area_t lease_end_area{};
        lv_obj_get_coords(enable_server_button, &enable_area);
        lv_obj_get_coords(server_back_button, &back_area);
        lv_obj_get_coords(server_inputs[3], &lease_end_area);
        assert(enable_area.y1 - lease_end_area.y2 - 1 == 4);
        assert(back_area.y1 - enable_area.y2 - 1 == 4);
        lv_obj_t* const server_keyboard = find_keyboard(lv_screen_active());
        assert(server_keyboard != nullptr);
        lv_area_t keyboard_area{};
        lv_obj_get_coords(server_keyboard, &keyboard_area);
        assert(keyboard_area.y1 == 328);
        assert(lv_obj_get_height(server_keyboard) == 152);

        UiControlCommand enable_server;
        enable_server.type = UiControlCommandType::Tap;
        enable_server.x = 160;
        enable_server.y = 270;
        const UiControlResponse server_confirmation = dispatch(event_queue, enable_server, 25U);
        assert(server_confirmation.ok);
        assert(server_confirmation.screen_id == "netsettings");
        const UiControlResponse server_result = dispatch(event_queue, enable_server, 26U);
        assert(server_result.ok);
        assert(server_result.screen_id == "network_result");
        assert(network_request.has_value());
        const auto* server_request =
            std::get_if<micropanel_touch::core::DhcpServerOperation>(&*network_request);
        assert(server_request != nullptr);
        assert(server_request->interface_name == "eth0");
        assert(server_request->settings.address == "192.168.50.1");
        assert(server_request->settings.prefix_length == "24");
        assert(server_request->settings.lease_start == "192.168.50.101");
        assert(server_request->settings.lease_end == "192.168.50.200");

        const UiControlResponse root_after_network = dispatch(event_queue, back, 27U);
        assert(root_after_network.ok);
        assert(root_after_network.screen_id == "network_menu");
        const UiControlResponse root_after_network_menu = dispatch(event_queue, back, 28U);
        assert(root_after_network_menu.ok);
        assert(root_after_network_menu.screen_id == "root");
        lv_obj_t* const display_button = find_button_with_text(lv_screen_active(), "Display");
        assert(display_button != nullptr);
        lv_area_t display_area{};
        lv_obj_get_coords(display_button, &display_area);
        UiControlCommand tap_display;
        tap_display.type = UiControlCommandType::Tap;
        tap_display.x = (display_area.x1 + display_area.x2) / 2;
        tap_display.y = (display_area.y1 + display_area.y2) / 2;
        const UiControlResponse display_menu = dispatch(event_queue, tap_display, 29U);
        assert(display_menu.ok);
        assert(display_menu.screen_id == "display_menu");
        // Orientation is disabled in the shipping config until it is
        // implemented, so it must not be rendered at all - a tile that does
        // nothing is worse than one that is not there.
        assert(find_button_with_text(lv_screen_active(), "Orientation") == nullptr);
        {
            int checked = 0;
            assert_buttons_within(lv_screen_active(), 320, 480, checked);
            assert(checked == 4);            // four tiles, none needing a scroll
        }
        for (const char* const title : {"Brightness", "Standby", "Theme", "Back"}) {
            lv_obj_t* const menu_button = find_button_with_text(lv_screen_active(), title);
            assert(menu_button != nullptr);
            lv_area_t menu_button_area{};
            lv_obj_get_coords(menu_button, &menu_button_area);
            assert(menu_button_area.y1 >= 52);
            assert(menu_button_area.y2 <= 467);
            assert(lv_obj_get_height(menu_button) == 133);
        }
        lv_obj_t* const brightness_button = find_button_with_text(lv_screen_active(), "Brightness");
        assert(brightness_button != nullptr);
        lv_area_t brightness_button_area{};
        lv_obj_get_coords(brightness_button, &brightness_button_area);
        UiControlCommand tap_brightness;
        tap_brightness.type = UiControlCommandType::Tap;
        tap_brightness.x = (brightness_button_area.x1 + brightness_button_area.x2) / 2;
        tap_brightness.y = (brightness_button_area.y1 + brightness_button_area.y2) / 2;
        const UiControlResponse brightness_screen = dispatch(event_queue, tap_brightness, 30U);
        assert(brightness_screen.ok);
        assert(brightness_screen.screen_id == "brightness");
        lv_obj_t* const brightness_control = find_slider(lv_screen_active());
        assert(brightness_control != nullptr);
        assert(lv_slider_get_value(brightness_control) == 100);
        assert(find_button_with_text(lv_screen_active(), "Apply") == nullptr);
        assert(find_button_with_text(lv_screen_active(), "Back") != nullptr);
        lv_slider_set_value(brightness_control, 37, LV_ANIM_OFF);
        lv_obj_send_event(brightness_control, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(display_brightness_settings.percent == 100U);
        assert(display_brightness_preview_percent == 37U);
        assert(display_brightness_preview_count == 1U);
        assert(display_brightness_apply_count == 0U);
        lv_obj_send_event(brightness_control, LV_EVENT_RELEASED, nullptr);
        assert(display_brightness_settings.percent == 37U);
        assert(display_brightness_apply_count == 1U);
        // Nothing in this whole scripted walkthrough may erase the device. The
        // reset needs its own screen, a PIN when the lock is on, and two
        // deliberate presses; a stray tap must never reach it.
        assert(factory_reset_requests == 0U);
        ui.return_to_home();
        lv_obj_t* const display_button_again = find_button_with_text(lv_screen_active(), "Display");
        assert(display_button_again != nullptr);
        lv_obj_get_coords(display_button_again, &display_area);
        tap_display.x = (display_area.x1 + display_area.x2) / 2;
        tap_display.y = (display_area.y1 + display_area.y2) / 2;
        const UiControlResponse display_menu_again = dispatch(event_queue, tap_display, 32U);
        assert(display_menu_again.ok);
        assert(display_menu_again.screen_id == "display_menu");
        lv_obj_t* const display_standby_button =
            find_button_with_text(lv_screen_active(), "Standby");
        assert(display_standby_button != nullptr);
        lv_area_t display_standby_button_area{};
        lv_obj_get_coords(display_standby_button, &display_standby_button_area);
        UiControlCommand tap_display_standby;
        tap_display_standby.type = UiControlCommandType::Tap;
        tap_display_standby.x = (display_standby_button_area.x1 + display_standby_button_area.x2) / 2;
        tap_display_standby.y = (display_standby_button_area.y1 + display_standby_button_area.y2) / 2;
        const UiControlResponse display_standby_screen =
            dispatch(event_queue, tap_display_standby, 33U);
        assert(display_standby_screen.ok);
        assert(display_standby_screen.screen_id == "display_standby");
        lv_obj_t* const standby_slider = find_slider(lv_screen_active());
        lv_obj_t* const standby_checkbox = find_checkbox(lv_screen_active());
        assert(standby_slider != nullptr);
        assert(standby_checkbox != nullptr);
        assert(lv_slider_get_value(standby_slider) == 60);
        assert(!lv_obj_has_state(standby_slider, LV_STATE_DISABLED));
        lv_obj_t* const standby_apply_button = find_button_with_text(lv_screen_active(), "Apply");
        assert(standby_apply_button != nullptr);
        assert(lv_obj_has_state(standby_apply_button, LV_STATE_DISABLED));
        lv_slider_set_value(standby_slider, 124, LV_ANIM_OFF);
        lv_obj_send_event(standby_slider, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_send_event(standby_slider, LV_EVENT_RELEASED, nullptr);
        assert(display_standby_settings.enabled);
        assert(display_standby_settings.seconds == 60U);
        assert(display_standby_apply_count == 0U);
        assert(!lv_obj_has_state(standby_apply_button, LV_STATE_DISABLED));
        lv_obj_remove_state(standby_checkbox, LV_STATE_CHECKED);
        lv_obj_send_event(standby_checkbox, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(display_standby_settings.enabled);
        assert(lv_obj_has_state(standby_slider, LV_STATE_DISABLED));
        assert(!lv_obj_has_state(standby_apply_button, LV_STATE_DISABLED));
        lv_area_t standby_apply_area{};
        lv_obj_get_coords(standby_apply_button, &standby_apply_area);
        UiControlCommand tap_standby_apply;
        tap_standby_apply.type = UiControlCommandType::Tap;
        tap_standby_apply.x = (standby_apply_area.x1 + standby_apply_area.x2) / 2;
        tap_standby_apply.y = (standby_apply_area.y1 + standby_apply_area.y2) / 2;
        const UiControlResponse applied_standby = dispatch(event_queue, tap_standby_apply, 34U);
        assert(applied_standby.ok);
        assert(!display_standby_settings.enabled);
        assert(display_standby_settings.seconds == 120U);
        assert(display_standby_apply_count == 1U);
        assert(lv_obj_has_state(standby_apply_button, LV_STATE_DISABLED));
        ui.return_to_home();
        UiControlCommand state_after_standby;
        state_after_standby.type = UiControlCommandType::State;
        const UiControlResponse root_after_standby = dispatch(event_queue, state_after_standby, 35U);
        assert(root_after_standby.ok);
        assert(root_after_standby.screen_id == "root");
        lv_obj_t* system_button_again = find_button_with_text(lv_screen_active(), "System");
        assert(system_button_again != nullptr);
        lv_area_t system_button_again_area{};
        lv_obj_get_coords(system_button_again, &system_button_again_area);
        UiControlCommand tap_system_again;
        tap_system_again.type = UiControlCommandType::Tap;
        tap_system_again.x = (system_button_again_area.x1 + system_button_again_area.x2) / 2;
        tap_system_again.y = (system_button_again_area.y1 + system_button_again_area.y2) / 2;
        const UiControlResponse system_menu_again = dispatch(event_queue, tap_system_again, 36U);
        assert(system_menu_again.ok);
        assert(system_menu_again.screen_id == "system_menu");
        assert(find_button_with_text(lv_screen_active(), "Standby") == nullptr);
        lv_obj_t* screen_lock_button = find_button_with_text(lv_screen_active(), "Screen Lock");
        assert(screen_lock_button != nullptr);
        lv_area_t screen_lock_button_area{};
        lv_obj_get_coords(screen_lock_button, &screen_lock_button_area);
        UiControlCommand tap_screen_lock;
        tap_screen_lock.type = UiControlCommandType::Tap;
        tap_screen_lock.x = (screen_lock_button_area.x1 + screen_lock_button_area.x2) / 2;
        tap_screen_lock.y = (screen_lock_button_area.y1 + screen_lock_button_area.y2) / 2;
        const UiControlResponse screen_lock_settings_screen =
            dispatch(event_queue, tap_screen_lock, 121U);
        assert(screen_lock_settings_screen.ok);
        assert(screen_lock_settings_screen.screen_id == "screen_lock_settings");
        assert(find_button_with_text(lv_screen_active(), "Set PIN") != nullptr);
        lv_obj_t* const enable_lock_button =
            find_button_with_text(lv_screen_active(), "Enable screen lock");
        assert(enable_lock_button != nullptr);
        lv_area_t enable_lock_area{};
        lv_obj_get_coords(enable_lock_button, &enable_lock_area);
        UiControlCommand tap_enable_lock;
        tap_enable_lock.type = UiControlCommandType::Tap;
        tap_enable_lock.x = (enable_lock_area.x1 + enable_lock_area.x2) / 2;
        tap_enable_lock.y = (enable_lock_area.y1 + enable_lock_area.y2) / 2;
        const UiControlResponse enable_without_pin = dispatch(event_queue, tap_enable_lock, 122U);
        assert(enable_without_pin.ok);
        assert(!screen_lock_settings.enabled);
        const UiControlResponse missing_pin_tree = dispatch(event_queue, capture_tree, 123U);
        assert(std::any_of(missing_pin_tree.widgets.begin(), missing_pin_tree.widgets.end(),
                           [](const auto& widget) {
                               return widget.text == "Set a PIN before enabling screen lock.";
                           }));
        lv_obj_t* const set_pin_button = find_button_with_text(lv_screen_active(), "Set PIN");
        assert(set_pin_button != nullptr);
        lv_area_t set_pin_area{};
        lv_obj_get_coords(set_pin_button, &set_pin_area);
        UiControlCommand tap_set_pin;
        tap_set_pin.type = UiControlCommandType::Tap;
        tap_set_pin.x = (set_pin_area.x1 + set_pin_area.x2) / 2;
        tap_set_pin.y = (set_pin_area.y1 + set_pin_area.y2) / 2;
        const UiControlResponse pin_setup = dispatch(event_queue, tap_set_pin, 124U);
        assert(pin_setup.ok);
        assert(pin_setup.screen_id == "screen_lock_pin_setup");
        std::vector<lv_obj_t*> lock_inputs;
        collect_textareas(lv_screen_active(), &lock_inputs);
        assert(lock_inputs.size() == 2U);
        for (lv_obj_t* const input : lock_inputs) {
            assert(lv_textarea_get_password_mode(input));
        }
        std::vector<lv_obj_t*> setup_visibility_buttons;
        collect_buttons_with_text(lv_screen_active(), LV_SYMBOL_EYE_OPEN, &setup_visibility_buttons);
        assert(setup_visibility_buttons.size() == 2U);
        for (std::size_t index = 0U; index < setup_visibility_buttons.size(); ++index) {
            lv_obj_add_state(setup_visibility_buttons[index], LV_STATE_CHECKED);
            lv_obj_send_event(setup_visibility_buttons[index], LV_EVENT_VALUE_CHANGED, nullptr);
            assert(!lv_textarea_get_password_mode(lock_inputs[index]));
            lv_obj_remove_state(setup_visibility_buttons[index], LV_STATE_CHECKED);
            lv_obj_send_event(setup_visibility_buttons[index], LV_EVENT_VALUE_CHANGED, nullptr);
            assert(lv_textarea_get_password_mode(lock_inputs[index]));
        }
        UiControlCommand forbidden_lock_text;
        forbidden_lock_text.type = UiControlCommandType::Text;
        forbidden_lock_text.target = "ip_address";
        forbidden_lock_text.text = "1234";
        const UiControlResponse rejected_lock_text =
            dispatch(event_queue, forbidden_lock_text, 125U);
        assert(!rejected_lock_text.ok);
        assert(rejected_lock_text.error == "text injection is forbidden for password fields");
        lv_textarea_set_text(lock_inputs[0], "1234");
        lv_textarea_set_text(lock_inputs[1], "1234");
        lv_obj_t* const save_pin_button = find_button_with_text(lv_screen_active(), "Save PIN");
        assert(save_pin_button != nullptr);
        lv_area_t save_pin_area{};
        lv_obj_get_coords(save_pin_button, &save_pin_area);
        UiControlCommand tap_save_pin;
        tap_save_pin.type = UiControlCommandType::Tap;
        tap_save_pin.x = (save_pin_area.x1 + save_pin_area.x2) / 2;
        tap_save_pin.y = (save_pin_area.y1 + save_pin_area.y2) / 2;
        const UiControlResponse pin_saved = dispatch(event_queue, tap_save_pin, 126U);
        assert(pin_saved.ok);
        assert(pin_saved.screen_id == "screen_lock_settings");
        assert(screen_lock_settings.configured);
        assert(!screen_lock_settings.enabled);
        lv_obj_t* const enable_configured_lock =
            find_button_with_text(lv_screen_active(), "Enable screen lock");
        assert(enable_configured_lock != nullptr);
        lv_obj_get_coords(enable_configured_lock, &enable_lock_area);
        tap_enable_lock.x = (enable_lock_area.x1 + enable_lock_area.x2) / 2;
        tap_enable_lock.y = (enable_lock_area.y1 + enable_lock_area.y2) / 2;
        const UiControlResponse lock_enabled = dispatch(event_queue, tap_enable_lock, 127U);
        assert(lock_enabled.ok);
        assert(screen_lock_settings.enabled);
        lv_obj_t* const lock_now_button = find_button_with_text(lv_screen_active(), "Lock now");
        assert(lock_now_button != nullptr);
        lv_area_t lock_now_area{};
        lv_obj_get_coords(lock_now_button, &lock_now_area);
        UiControlCommand tap_lock_now;
        tap_lock_now.type = UiControlCommandType::Tap;
        tap_lock_now.x = (lock_now_area.x1 + lock_now_area.x2) / 2;
        tap_lock_now.y = (lock_now_area.y1 + lock_now_area.y2) / 2;
        const UiControlResponse locked = dispatch(event_queue, tap_lock_now, 128U);
        assert(locked.ok);
        assert(locked.screen_id == "screen_lock");
        assert(screen_lock_session_locked);
        UiControlCommand locked_back;
        locked_back.type = UiControlCommandType::Back;
        const UiControlResponse blocked_locked_back = dispatch(event_queue, locked_back, 129U);
        assert(blocked_locked_back.ok);
        assert(blocked_locked_back.screen_id == "screen_lock");
        std::vector<lv_obj_t*> unlock_inputs;
        collect_textareas(lv_screen_active(), &unlock_inputs);
        assert(unlock_inputs.size() == 1U);
        lv_obj_t* const unlock_visibility_button =
            find_button_with_text(lv_screen_active(), LV_SYMBOL_EYE_OPEN);
        assert(unlock_visibility_button != nullptr);
        lv_obj_add_state(unlock_visibility_button, LV_STATE_CHECKED);
        lv_obj_send_event(unlock_visibility_button, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(!lv_textarea_get_password_mode(unlock_inputs.front()));
        assert(find_button_with_text(lv_screen_active(), LV_SYMBOL_EYE_CLOSE) != nullptr);
        lv_textarea_set_text(unlock_inputs.front(), "0000");
        const UiControlResponse revealed_unlock_tree = dispatch(event_queue, capture_tree, 129U);
        assert(std::none_of(revealed_unlock_tree.widgets.begin(), revealed_unlock_tree.widgets.end(),
                           [](const auto& widget) { return widget.text.find("0000") != std::string::npos; }));
        lv_obj_remove_state(unlock_visibility_button, LV_STATE_CHECKED);
        lv_obj_send_event(unlock_visibility_button, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(lv_textarea_get_password_mode(unlock_inputs.front()));
        lv_textarea_set_text(unlock_inputs.front(), "0000");
        lv_obj_t* const unlock_button = find_button_with_text(lv_screen_active(), "Unlock");
        assert(unlock_button != nullptr);
        lv_area_t unlock_area{};
        lv_obj_get_coords(unlock_button, &unlock_area);
        UiControlCommand tap_unlock;
        tap_unlock.type = UiControlCommandType::Tap;
        tap_unlock.x = (unlock_area.x1 + unlock_area.x2) / 2;
        tap_unlock.y = (unlock_area.y1 + unlock_area.y2) / 2;
        const UiControlResponse wrong_unlock = dispatch(event_queue, tap_unlock, 130U);
        assert(wrong_unlock.ok);
        assert(wrong_unlock.screen_id == "screen_lock");
        lv_textarea_set_text(unlock_inputs.front(), "1234");
        const UiControlResponse unlocked = dispatch(event_queue, tap_unlock, 131U);
        assert(unlocked.ok);
        assert(unlocked.screen_id == "root");
        assert(!screen_lock_session_locked);
        lv_obj_t* const system_button_after_unlock = find_button_with_text(lv_screen_active(), "System");
        assert(system_button_after_unlock != nullptr);
        lv_obj_get_coords(system_button_after_unlock, &system_button_again_area);
        tap_system_again.x = (system_button_again_area.x1 + system_button_again_area.x2) / 2;
        tap_system_again.y = (system_button_again_area.y1 + system_button_again_area.y2) / 2;
        assert(dispatch(event_queue, tap_system_again, 132U).screen_id == "system_menu");
        screen_lock_button = find_button_with_text(lv_screen_active(), "Screen Lock");
        assert(screen_lock_button != nullptr);
        lv_obj_get_coords(screen_lock_button, &screen_lock_button_area);
        tap_screen_lock.x = (screen_lock_button_area.x1 + screen_lock_button_area.x2) / 2;
        tap_screen_lock.y = (screen_lock_button_area.y1 + screen_lock_button_area.y2) / 2;
        assert(dispatch(event_queue, tap_screen_lock, 133U).screen_id == "screen_lock_settings");
        lv_obj_t* const disable_lock_button =
            find_button_with_text(lv_screen_active(), "Disable screen lock");
        assert(disable_lock_button != nullptr);
        lv_area_t disable_lock_area{};
        lv_obj_get_coords(disable_lock_button, &disable_lock_area);
        UiControlCommand tap_disable_lock;
        tap_disable_lock.type = UiControlCommandType::Tap;
        tap_disable_lock.x = (disable_lock_area.x1 + disable_lock_area.x2) / 2;
        tap_disable_lock.y = (disable_lock_area.y1 + disable_lock_area.y2) / 2;
        assert(dispatch(event_queue, tap_disable_lock, 134U).screen_id == "screen_lock_disable");
        std::vector<lv_obj_t*> disable_inputs;
        collect_textareas(lv_screen_active(), &disable_inputs);
        assert(disable_inputs.size() == 1U);
        lv_obj_t* const disable_visibility_button =
            find_button_with_text(lv_screen_active(), LV_SYMBOL_EYE_OPEN);
        assert(disable_visibility_button != nullptr);
        lv_obj_add_state(disable_visibility_button, LV_STATE_CHECKED);
        lv_obj_send_event(disable_visibility_button, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(!lv_textarea_get_password_mode(disable_inputs.front()));
        lv_obj_remove_state(disable_visibility_button, LV_STATE_CHECKED);
        lv_obj_send_event(disable_visibility_button, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(lv_textarea_get_password_mode(disable_inputs.front()));
        lv_textarea_set_text(disable_inputs.front(), "1234");
        lv_obj_t* const confirm_disable_button =
            find_button_with_text(lv_screen_active(), "Disable screen lock");
        assert(confirm_disable_button != nullptr);
        lv_obj_get_coords(confirm_disable_button, &disable_lock_area);
        tap_disable_lock.x = (disable_lock_area.x1 + disable_lock_area.x2) / 2;
        tap_disable_lock.y = (disable_lock_area.y1 + disable_lock_area.y2) / 2;
        const UiControlResponse disabled = dispatch(event_queue, tap_disable_lock, 135U);
        assert(disabled.ok);
        assert(disabled.screen_id == "screen_lock_settings");
        assert(!screen_lock_settings.enabled);
        ui.return_to_home();
        assert(dispatch(event_queue, state_after_standby, 136U).screen_id == "root");
        system_button_again = find_button_with_text(lv_screen_active(), "System");
        assert(system_button_again != nullptr);
        lv_obj_get_coords(system_button_again, &system_button_again_area);
        tap_system_again.x = (system_button_again_area.x1 + system_button_again_area.x2) / 2;
        tap_system_again.y = (system_button_again_area.y1 + system_button_again_area.y2) / 2;
        assert(dispatch(event_queue, tap_system_again, 137U).screen_id == "system_menu");
        lv_obj_t* const calibration_button =
            find_button_with_text(lv_screen_active(), "Touch Calibration");
        assert(calibration_button != nullptr);
        lv_area_t calibration_button_area{};
        lv_obj_get_coords(calibration_button, &calibration_button_area);
        UiControlCommand tap_calibration;
        tap_calibration.type = UiControlCommandType::Tap;
        tap_calibration.x = (calibration_button_area.x1 + calibration_button_area.x2) / 2;
        tap_calibration.y = (calibration_button_area.y1 + calibration_button_area.y2) / 2;
        const UiControlResponse calibration_screen = dispatch(event_queue, tap_calibration, 34U);
        assert(calibration_screen.ok);
        assert(calibration_screen.screen_id == "touch_calibration");
        for (int target = 1; target <= 5; ++target) {
            lv_obj_t* const target_button =
                find_button_with_text(lv_screen_active(), std::to_string(target));
            assert(target_button != nullptr);
            lv_area_t target_area{};
            lv_obj_get_coords(target_button, &target_area);
            event_queue.push({static_cast<std::uint64_t>(100U + target),
                              micropanel_touch::core::TouchCalibrationRawSample{
                                  600 + target * 300, 700 + target * 350,
                                  (target_area.x1 + target_area.x2) / 2,
                                  (target_area.y1 + target_area.y2) / 2}});
            const UiControlResponse calibration_tree = dispatch(event_queue, capture_tree,
                                                                 static_cast<std::uint64_t>(110U + target));
            assert(calibration_tree.ok);
        }
        assert(applied_calibration_samples.size() == 5U);
        const UiControlResponse calibration_result = dispatch(event_queue, capture_tree, 116U);
        assert(calibration_result.ok);
        assert(std::any_of(calibration_result.widgets.begin(), calibration_result.widgets.end(),
                           [](const auto& widget) {
                               return widget.text == "Calibration saved and active. Test the keypad now.";
                           }));
        lv_obj_t* const reset_button = find_button_with_text(lv_screen_active(), "Reset default");
        assert(reset_button != nullptr);
        lv_area_t reset_area{};
        lv_obj_get_coords(reset_button, &reset_area);
        UiControlCommand tap_reset;
        tap_reset.type = UiControlCommandType::Tap;
        tap_reset.x = (reset_area.x1 + reset_area.x2) / 2;
        tap_reset.y = (reset_area.y1 + reset_area.y2) / 2;
        const UiControlResponse reset_confirmation = dispatch(event_queue, tap_reset, 117U);
        assert(reset_confirmation.ok);
        assert(calibration_reset_count == 0U);
        const UiControlResponse reset_confirmation_tree = dispatch(event_queue, capture_tree, 118U);
        assert(reset_confirmation_tree.ok);
        assert(std::any_of(reset_confirmation_tree.widgets.begin(),
                           reset_confirmation_tree.widgets.end(), [](const auto& widget) {
                               return widget.text ==
                                      "Tap Reset default again to restore the factory mapping.";
                           }));
        lv_obj_t* const confirm_reset_button = find_button_with_text(lv_screen_active(), "Confirm reset");
        assert(confirm_reset_button != nullptr);
        lv_area_t confirm_reset_area{};
        lv_obj_get_coords(confirm_reset_button, &confirm_reset_area);
        tap_reset.x = (confirm_reset_area.x1 + confirm_reset_area.x2) / 2;
        tap_reset.y = (confirm_reset_area.y1 + confirm_reset_area.y2) / 2;
        const UiControlResponse reset_result = dispatch(event_queue, tap_reset, 119U);
        assert(reset_result.ok);
        assert(calibration_reset_count == 1U);
        const UiControlResponse reset_result_tree = dispatch(event_queue, capture_tree, 120U);
        assert(reset_result_tree.ok);
        assert(std::any_of(reset_result_tree.widgets.begin(), reset_result_tree.widgets.end(),
                           [](const auto& widget) {
                               return widget.text ==
                                      "Factory mapping restored. Reopen this screen to calibrate.";
                           }));

        // --- Software Update: both routes, and the offer ----------------
        auto tap_at = [&](lv_obj_t* target, std::uint64_t id) {
            assert(target != nullptr);
            lv_area_t area{};
            lv_obj_get_coords(target, &area);
            UiControlCommand tap;
            tap.type = UiControlCommandType::Tap;
            tap.x = (area.x1 + area.x2) / 2;
            tap.y = (area.y1 + area.y2) / 2;
            return dispatch(event_queue, tap, id);
        };
        auto tree_has = [](const UiControlResponse& response, const std::string& text) {
            return std::any_of(response.widgets.begin(), response.widgets.end(),
                               [&text](const auto& widget) { return widget.text == text; });
        };

        ui.return_to_home();
        assert(dispatch(event_queue, capture_tree, 140U).ok);
        assert(tap_at(find_button_with_text(lv_screen_active(), "System"), 141U).screen_id ==
               "system_menu");
        for (const char* tile : {"System Stats", "About", "Software Update", "Power",
                                 "Factory Reset", "Screen Lock", "Touch Calibration", "Back"}) {
            assert(find_button_with_text(lv_screen_active(), tile) != nullptr);
        }
        {
            int checked = 0;
            assert_buttons_within(lv_screen_active(), 320, 480, checked);
            assert(checked == 8);            // a full 2x4 grid, none needing a scroll
        }
        const UiControlResponse update_screen =
            tap_at(find_button_with_text(lv_screen_active(), "Software Update"), 142U);
        assert(update_screen.screen_id == "software_update");
        // Both routes are offered, and the USB one is not quietly replaced by
        // the network one: an offline panel depends on it.
        assert(find_button_with_text(lv_screen_active(), "Check for updates") != nullptr);
        assert(find_button_with_text(lv_screen_active(), "Check USB stick") != nullptr);

        // An available release becomes an offer, and installs over the network.
        update_check_offers = true;
        const UiControlResponse offered =
            tap_at(find_button_with_text(lv_screen_active(), "Check for updates"), 143U);
        assert(offered.ok);
        const UiControlResponse offered_tree = dispatch(event_queue, capture_tree, 144U);
        assert(tree_has(offered_tree, "Update available: 00.99"));
        assert(requested_update_source.empty());   // checking must install nothing
        const UiControlResponse installing =
            tap_at(find_button_with_text(lv_screen_active(), "Update now"), 145U);
        assert(installing.ok);
        assert(requested_update_source == micropanel_touch::core::kSystemUpdateOtaSource);

        // Up to date offers nothing to press. A stale offer must not survive
        // into the next check.
        requested_update_source.clear();
        update_check_offers = false;
        ui.return_to_home();
        assert(dispatch(event_queue, capture_tree, 146U).ok);
        assert(tap_at(find_button_with_text(lv_screen_active(), "System"), 147U).screen_id ==
               "system_menu");
        assert(tap_at(find_button_with_text(lv_screen_active(), "Software Update"), 148U)
                   .screen_id == "software_update");
        assert(tap_at(find_button_with_text(lv_screen_active(), "Check for updates"), 149U).ok);
        const UiControlResponse current_tree = dispatch(event_queue, capture_tree, 150U);
        assert(tree_has(current_tree, "This panel is up to date (00.99)."));
        assert(find_button_with_text(lv_screen_active(), "Update now") == nullptr);
        assert(requested_update_source.empty());

        // --- Power: two presses, and the right one -----------------------
        auto open_power = [&](std::uint64_t id) {
            ui.return_to_home();
            assert(dispatch(event_queue, capture_tree, id).ok);
            assert(tap_at(find_button_with_text(lv_screen_active(), "System"), id + 1U).screen_id ==
                   "system_menu");
            assert(tap_at(find_button_with_text(lv_screen_active(), "Power"), id + 2U).screen_id ==
                   "power");
        };

        open_power(160U);
        // One press arms and does nothing else. A single tap that restarted
        // the panel would be indistinguishable from a mis-tap on the tile.
        assert(tap_at(find_button_with_text(lv_screen_active(), "Restart"), 163U).ok);
        assert(power_requests.empty());
        assert(tree_has(dispatch(event_queue, capture_tree, 164U),
                        "Press again to restart the panel."));
        assert(find_button_with_text(lv_screen_active(), "Confirm restart") != nullptr);
        // The second press on the *armed* control acts.
        assert(tap_at(find_button_with_text(lv_screen_active(), "Confirm restart"), 165U).ok);
        assert(power_requests.size() == 1U);
        assert(power_requests.at(0) == micropanel_touch::core::PowerAction::reboot);
        assert(tree_has(dispatch(event_queue, capture_tree, 166U), "Restarting..."));

        // Arming one action must not arm the other: a panel that shuts down
        // because the operator armed Restart and then pressed Shut down is a
        // panel somebody has to walk over to.
        power_requests.clear();
        open_power(170U);
        assert(tap_at(find_button_with_text(lv_screen_active(), "Restart"), 173U).ok);
        assert(tap_at(find_button_with_text(lv_screen_active(), "Shut down"), 174U).ok);
        assert(power_requests.empty());
        assert(find_button_with_text(lv_screen_active(), "Confirm shut down") != nullptr);
        // ...and the first control is back at rest, not still armed.
        assert(find_button_with_text(lv_screen_active(), "Confirm restart") == nullptr);
        assert(find_button_with_text(lv_screen_active(), "Restart") != nullptr);
        assert(tap_at(find_button_with_text(lv_screen_active(), "Confirm shut down"), 175U).ok);
        assert(power_requests.size() == 1U);
        assert(power_requests.at(0) == micropanel_touch::core::PowerAction::shutdown);

        // Leaving the screen disarms it. Otherwise the next visit would act on
        // a single press, which is the whole thing the confirm prevents.
        power_requests.clear();
        open_power(180U);
        assert(tap_at(find_button_with_text(lv_screen_active(), "Restart"), 183U).ok);
        assert(dispatch(event_queue, back_command, 184U).screen_id == "system_menu");
        assert(tap_at(find_button_with_text(lv_screen_active(), "Power"), 185U).screen_id == "power");
        assert(find_button_with_text(lv_screen_active(), "Confirm restart") == nullptr);
        assert(tap_at(find_button_with_text(lv_screen_active(), "Restart"), 186U).ok);
        assert(power_requests.empty());

        // A refused transition says the panel is still running, and disarms:
        // the operator has to decide again rather than press into a failure.
        power_requests.clear();
        power_available = false;
        open_power(190U);
        assert(tap_at(find_button_with_text(lv_screen_active(), "Restart"), 193U).ok);
        assert(tap_at(find_button_with_text(lv_screen_active(), "Confirm restart"), 194U).ok);
        assert(power_requests.empty());
        assert(tree_has(dispatch(event_queue, capture_tree, 195U),
                        "Restart could not be started; the panel is still running."));
        assert(find_button_with_text(lv_screen_active(), "Confirm restart") == nullptr);
    }
    lv_deinit();
    return 0;
}
