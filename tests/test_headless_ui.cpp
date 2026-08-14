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

        const auto config = micropanel_touch::ui::StarterConfig::load(config_path, &diagnostic);
        assert(config.has_value());
        UiEventQueue event_queue;
        micropanel_touch::platform::SyntheticTouchInput synthetic_touch;
        micropanel_touch::platform::SyntheticKeypadInput synthetic_keypad;
        assert(synthetic_touch.attach(&diagnostic));
        assert(synthetic_keypad.attach(&diagnostic));
        std::optional<micropanel_touch::core::NetworkOperation> network_request;
        std::vector<micropanel_touch::platform::TouchCalibrationSample> applied_calibration_samples;
        unsigned int calibration_reset_count = 0U;
        micropanel_touch::platform::DisplayStandbySettings display_standby_settings{true, 60U};
        unsigned int display_standby_apply_count = 0U;
        micropanel_touch::platform::DisplayBrightnessSettings display_brightness_settings{100U};
        unsigned int display_brightness_preview_percent = 100U;
        unsigned int display_brightness_preview_count = 0U;
        unsigned int display_brightness_apply_count = 0U;

        micropanel_touch::ui::StarterUi ui(
            *config, theme, event_queue, &synthetic_touch, &synthetic_keypad,
            [&display](std::string* capture_diagnostic) { return display.capture(capture_diagnostic); },
            [] {}, [] {}, "eth0",
            [&event_queue, &network_request](
                std::uint64_t request_id,
                const micropanel_touch::core::NetworkOperation& operation,
                std::string*) {
                network_request = operation;
                const bool is_dhcp =
                    std::holds_alternative<micropanel_touch::core::DhcpOperation>(operation);
                const bool is_dhcp_server =
                    std::holds_alternative<micropanel_touch::core::DhcpServerOperation>(operation);
                event_queue.push({90U, micropanel_touch::core::NetworkApplyResult{
                                          request_id, true,
                                          is_dhcp ? "DHCP applied."
                                                  : (is_dhcp_server ? "DHCP server applied."
                                                                    : "Static IP applied.")}});
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
            [](micropanel_touch::platform::TouchPoint point) { return point; });
        ui.start();

        UiControlCommand capture_tree;
        capture_tree.type = UiControlCommandType::CaptureTree;
        const UiControlResponse root = dispatch(event_queue, capture_tree, 1U);
        assert(root.ok);
        assert(root.screen_id == "root");
        assert(std::any_of(root.widgets.begin(), root.widgets.end(), [](const auto& widget) {
            return widget.text == "MicroPanel Touch";
        }));
        assert(std::any_of(root.widgets.begin(), root.widgets.end(), [](const auto& widget) {
            return widget.text.find("Network") != std::string::npos;
        }));

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

        UiControlCommand tap_password;
        tap_password.type = UiControlCommandType::Tap;
        tap_password.x = 160;
        tap_password.y = 244;
        const UiControlResponse password_screen = dispatch(event_queue, tap_password, 4U);
        assert(password_screen.ok);
        assert(password_screen.screen_id == "wifi_password_demo");

        lv_obj_t* const password_input = find_textarea(lv_screen_active());
        assert(password_input != nullptr);
        assert(synthetic_keypad.focus(password_input, &diagnostic));
        constexpr char kSecret[] = "otter42";
        assert(synthetic_keypad.type(kSecret, &diagnostic));
        lv_obj_update_layout(lv_screen_active());
        lv_refr_now(display.display());

        UiControlCommand forbidden_text;
        forbidden_text.type = UiControlCommandType::Text;
        forbidden_text.target = "ip_address";
        forbidden_text.text = kSecret;
        const UiControlResponse rejected = dispatch(event_queue, forbidden_text, 5U);
        assert(!rejected.ok);
        assert(rejected.error == "text injection is forbidden for password fields");

        const UiControlResponse password_tree = dispatch(event_queue, capture_tree, 6U);
        assert(password_tree.ok);
        assert(password_tree.screen_id == "wifi_password_demo");
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

        UiControlCommand back;
        back.type = UiControlCommandType::Back;
        const UiControlResponse network_again = dispatch(event_queue, back, 7U);
        assert(network_again.ok);
        assert(network_again.screen_id == "network_menu");

        UiControlCommand tap_ip_settings;
        tap_ip_settings.type = UiControlCommandType::Tap;
        tap_ip_settings.x = 160;
        tap_ip_settings.y = 132;
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
        const UiControlResponse dhcp_result_tree = dispatch(event_queue, capture_tree, 21U);
        assert(dhcp_result_tree.ok);
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
            find_button_with_text(lv_screen_active(), "Display Standby");
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
        lv_obj_t* const system_button_again = find_button_with_text(lv_screen_active(), "System");
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
        assert(find_button_with_text(lv_screen_active(), "Display Standby") == nullptr);
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
    }
    lv_deinit();
    return 0;
}
