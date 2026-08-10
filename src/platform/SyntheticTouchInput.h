#pragma once

#include <deque>
#include <string>

#include <lvgl.h>

namespace micropanel_touch::platform {

// A development-only LVGL pointer device. Synthetic samples are dispatched by
// the UI thread through the same pointer hit-testing/callback path as a real
// touch device; it never opens or writes a kernel input node.
class SyntheticTouchInput {
public:
    SyntheticTouchInput() = default;
    ~SyntheticTouchInput();
    SyntheticTouchInput(const SyntheticTouchInput&) = delete;
    SyntheticTouchInput& operator=(const SyntheticTouchInput&) = delete;

    bool attach(std::string* diagnostic);
    bool tap(int x, int y, std::string* diagnostic);

private:
    struct Report {
        int x{0};
        int y{0};
        bool pressed{false};
    };

    static void read_callback(lv_indev_t* indev, lv_indev_data_t* data);
    void read(lv_indev_data_t* data);

    lv_indev_t* indev_{nullptr};
    std::deque<Report> reports_;
    Report current_{};
};

}  // namespace micropanel_touch::platform
