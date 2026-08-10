#pragma once

#include <future>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace micropanel_touch::core {

// The control transport parses these simple commands off the UI thread. Their
// dispatch and every resulting LVGL operation remain on the UI event loop.
enum class UiControlCommandType {
    State,
    Navigate,
    Activate,
    Back,
    CaptureTree,
    CaptureFrame,
    Tap,
    Text,
};

struct UiControlCommand {
    UiControlCommandType type{UiControlCommandType::State};
    std::string target;
    std::string text;
    std::int32_t x{0};
    std::int32_t y{0};
};

// Flat preorder avoids coupling the transport to LVGL pointers while keeping
// parentage, hit-test geometry, and visible text straightforward to assert.
struct UiWidgetSnapshot {
    std::uint32_t id{0};
    std::int32_t parent_id{-1};
    std::string type;
    std::string text;
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t width{0};
    std::int32_t height{0};
    bool redacted{false};
    bool text_truncated{false};
};

// The UI thread owns the render-settle barrier and captures this immutable
// framebuffer payload before replying. Keeping it on the control response
// prevents the socket worker from racing a later LVGL repaint.
struct UiFrameCapture {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t stride_bytes{0};
    std::vector<std::uint8_t> pixels;
};

struct UiControlResponse {
    UiControlResponse() = default;
    UiControlResponse(bool response_ok, std::string response_screen_id,
                      std::vector<std::string> response_menu_path,
                      std::vector<UiWidgetSnapshot> response_widgets,
                      bool response_widget_tree_truncated, std::string response_error)
        : ok(response_ok), screen_id(std::move(response_screen_id)),
          menu_path(std::move(response_menu_path)), widgets(std::move(response_widgets)),
          widget_tree_truncated(response_widget_tree_truncated), error(std::move(response_error)) {}

    bool ok{false};
    std::string screen_id;
    std::vector<std::string> menu_path;
    std::vector<UiWidgetSnapshot> widgets;
    bool widget_tree_truncated{false};
    std::string error;
    std::optional<UiFrameCapture> frame_capture;
};

struct UiControlRequest {
    UiControlCommand command;
    std::shared_ptr<std::promise<UiControlResponse>> completion;
};

}  // namespace micropanel_touch::core
