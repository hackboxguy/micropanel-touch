#include "platform/SyntheticKeypadInput.h"

namespace micropanel_touch::platform {

SyntheticKeypadInput::~SyntheticKeypadInput() {
    if (indev_ != nullptr) {
        lv_indev_delete(indev_);
    }
    if (group_ != nullptr) {
        lv_group_delete(group_);
    }
}

bool SyntheticKeypadInput::attach(std::string* diagnostic) {
    if (indev_ != nullptr) {
        return true;
    }
    group_ = lv_group_create();
    if (group_ == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = "unable to create synthetic LVGL keypad group";
        }
        return false;
    }
    indev_ = lv_indev_create();
    if (indev_ == nullptr) {
        lv_group_delete(group_);
        group_ = nullptr;
        if (diagnostic != nullptr) {
            *diagnostic = "unable to create synthetic LVGL keypad device";
        }
        return false;
    }
    lv_indev_set_type(indev_, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_group(indev_, group_);
    lv_indev_set_user_data(indev_, this);
    lv_indev_set_read_cb(indev_, read_callback);
    return true;
}

bool SyntheticKeypadInput::focus(lv_obj_t* textarea, std::string* diagnostic) {
    if (group_ == nullptr || indev_ == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = "synthetic keypad device is not attached";
        }
        return false;
    }
    if (textarea == nullptr || !lv_obj_check_type(textarea, &lv_textarea_class)) {
        if (diagnostic != nullptr) {
            *diagnostic = "synthetic keypad focus requires a textarea";
        }
        return false;
    }
    lv_group_remove_all_objs(group_);
    lv_group_add_obj(group_, textarea);
    lv_group_focus_obj(textarea);
    return true;
}

bool SyntheticKeypadInput::is_focused(const lv_obj_t* textarea) const {
    return group_ != nullptr && textarea != nullptr && lv_group_get_focused(group_) == textarea;
}

bool SyntheticKeypadInput::type(const std::string& text, std::string* diagnostic) {
    if (indev_ == nullptr || group_ == nullptr || lv_group_get_focused(group_) == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = "synthetic keypad has no focused field";
        }
        return false;
    }
    if (text.empty()) {
        if (diagnostic != nullptr) {
            *diagnostic = "text must not be empty";
        }
        return false;
    }

    reports_.clear();
    for (const unsigned char character : text) {
        reports_.push_back({character, true});
        reports_.push_back({character, false});
    }
    // Like SyntheticTouchInput::tap(), this runs only from the UI thread. The
    // keypad indev uses its LVGL group and emits normal LV_EVENT_KEY events
    // to the focused textarea; no widget text is assigned directly.
    lv_indev_read(indev_);
    return true;
}

void SyntheticKeypadInput::read_callback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* const input = static_cast<SyntheticKeypadInput*>(lv_indev_get_user_data(indev));
    input->read(data);
}

void SyntheticKeypadInput::read(lv_indev_data_t* data) {
    if (!reports_.empty()) {
        current_ = reports_.front();
        reports_.pop_front();
    }
    data->key = current_.key;
    data->state = current_.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = !reports_.empty();
}

}  // namespace micropanel_touch::platform
