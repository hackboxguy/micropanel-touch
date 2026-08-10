#pragma once

#include <future>
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
};

struct UiControlCommand {
    UiControlCommandType type{UiControlCommandType::State};
    std::string target;
};

struct UiControlResponse {
    bool ok{false};
    std::string screen_id;
    std::vector<std::string> menu_path;
    std::string error;
};

struct UiControlRequest {
    UiControlCommand command;
    std::shared_ptr<std::promise<UiControlResponse>> completion;
};

}  // namespace micropanel_touch::core
