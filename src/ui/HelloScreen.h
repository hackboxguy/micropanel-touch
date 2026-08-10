#pragma once

#include <lvgl.h>

namespace micropanel_touch::ui {

class HelloScreen {
public:
    void create();

private:
    static void increment_counter(lv_event_t* event);
    void update_counter_label() const;

    lv_obj_t* counter_label_{nullptr};
    unsigned int counter_{0};
};

}  // namespace micropanel_touch::ui
