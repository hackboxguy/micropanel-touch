#include "platform/SyntheticTouchInput.h"

namespace micropanel_touch::platform {

SyntheticTouchInput::~SyntheticTouchInput() {
    if (indev_ != nullptr) {
        lv_indev_delete(indev_);
    }
}

bool SyntheticTouchInput::attach(std::string* diagnostic) {
    if (indev_ != nullptr) {
        return true;
    }
    indev_ = lv_indev_create();
    if (indev_ == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = "unable to create synthetic LVGL pointer device";
        }
        return false;
    }
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(indev_, this);
    lv_indev_set_read_cb(indev_, read_callback);
    return true;
}

bool SyntheticTouchInput::tap(int x, int y, std::string* diagnostic) {
    if (indev_ == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = "synthetic pointer device is not attached";
        }
        return false;
    }
    const int width = lv_display_get_horizontal_resolution(nullptr);
    const int height = lv_display_get_vertical_resolution(nullptr);
    if (x < 0 || y < 0 || x >= width || y >= height) {
        if (diagnostic != nullptr) {
            *diagnostic = "tap coordinates are outside the active display";
        }
        return false;
    }

    reports_.clear();
    reports_.push_back({x, y, true});
    reports_.push_back({x, y, false});
    // This function is called only from the LVGL/UI thread after command
    // dequeue. Running the pointer indev now retains LVGL's native hit-test,
    // pressed/released, and click event flow without a worker touching LVGL.
    lv_indev_read(indev_);
    return true;
}

void SyntheticTouchInput::read_callback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* const input = static_cast<SyntheticTouchInput*>(lv_indev_get_user_data(indev));
    input->read(data);
}

void SyntheticTouchInput::read(lv_indev_data_t* data) {
    if (!reports_.empty()) {
        current_ = reports_.front();
        reports_.pop_front();
    }
    data->point.x = current_.x;
    data->point.y = current_.y;
    data->state = current_.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = !reports_.empty();
}

}  // namespace micropanel_touch::platform
