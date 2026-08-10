#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include <lvgl.h>

namespace micropanel_touch::platform {

// Development-only keypad input used by the control socket. Text is delivered
// through LVGL's regular keypad/group event path; callers must explicitly
// focus a non-secret textarea before it can receive a character.
class SyntheticKeypadInput {
public:
    SyntheticKeypadInput() = default;
    ~SyntheticKeypadInput();
    SyntheticKeypadInput(const SyntheticKeypadInput&) = delete;
    SyntheticKeypadInput& operator=(const SyntheticKeypadInput&) = delete;

    bool attach(std::string* diagnostic);
    bool focus(lv_obj_t* textarea, std::string* diagnostic);
    bool is_focused(const lv_obj_t* textarea) const;
    bool type(const std::string& text, std::string* diagnostic);

private:
    struct Report {
        std::uint32_t key{0U};
        bool pressed{false};
    };

    static void read_callback(lv_indev_t* indev, lv_indev_data_t* data);
    void read(lv_indev_data_t* data);

    lv_indev_t* indev_{nullptr};
    lv_group_t* group_{nullptr};
    std::deque<Report> reports_;
    Report current_{};
};

}  // namespace micropanel_touch::platform
