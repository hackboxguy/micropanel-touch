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
#include <string>
#include <utility>

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

        micropanel_touch::ui::StarterUi ui(
            *config, theme, event_queue, &synthetic_touch, &synthetic_keypad,
            [&display](std::string* capture_diagnostic) { return display.capture(capture_diagnostic); },
            [] {}, [](std::uint64_t) { return true; }, [] {}, [](std::uint64_t) {},
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
    }
    lv_deinit();
    return 0;
}
