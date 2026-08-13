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
            [&theme] { return theme.active_skin().name; });
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
        const lv_coord_t field_x = 98;
        const lv_coord_t field_width = lv_obj_get_width(static_inputs.front());
        for (lv_obj_t* const input : static_inputs) {
            lv_area_t field_area{};
            lv_obj_get_coords(input, &field_area);
            assert(field_area.x1 == field_x);
            assert(lv_obj_get_width(input) == field_width);
        }
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

        UiControlCommand enable_server;
        enable_server.type = UiControlCommandType::Tap;
        enable_server.x = 160;
        enable_server.y = 298;
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
        assert(server_request->settings.lease_start == "192.168.50.100");
        assert(server_request->settings.lease_end == "192.168.50.200");
    }
    lv_deinit();
    return 0;
}
