#pragma once

#include <future>
#include <cstdint>
#include <memory>
#include <string>
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
};

struct UiControlCommand {
    UiControlCommandType type{UiControlCommandType::State};
    std::string target;
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

struct UiControlResponse {
    bool ok{false};
    std::string screen_id;
    std::vector<std::string> menu_path;
    std::vector<UiWidgetSnapshot> widgets;
    bool widget_tree_truncated{false};
    std::string error;
};

struct UiControlRequest {
    UiControlCommand command;
    std::shared_ptr<std::promise<UiControlResponse>> completion;
};

}  // namespace micropanel_touch::core
