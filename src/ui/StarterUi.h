#pragma once

#include "core/UiEventQueue.h"
#include "ui/StarterConfig.h"

#include <memory>
#include <string>
#include <vector>

#include <lvgl.h>

namespace micropanel_touch::ui {

class StarterUi {
public:
    StarterUi(StarterConfig config, core::UiEventQueue& event_queue);
    ~StarterUi();
    StarterUi(const StarterUi&) = delete;
    StarterUi& operator=(const StarterUi&) = delete;

    void start();

private:
    struct Action {
        StarterUi* ui;
        std::string id;
    };

    void show_root();
    void show_menu(const StarterModule& menu);
    void show_network_info();
    void show_placeholder(const std::string& title);
    void activate(const std::string& id);
    void clear_screen();
    void create_title(const std::string& title);
    void create_button(const std::string& title, int y, const std::string& action);
    void refresh_network_info();
    void drain_events();

    static void action_callback(lv_event_t* event);
    static void drain_timer_callback(lv_timer_t* timer);

    StarterConfig config_;
    core::UiEventQueue& event_queue_;
    std::vector<std::unique_ptr<Action>> actions_;
    core::NetworkSnapshot network_snapshot_;
    std::string network_text_;
    std::string current_menu_id_;
    bool network_info_visible_{false};
    lv_obj_t* network_label_{nullptr};
    lv_timer_t* event_timer_{nullptr};
};

}  // namespace micropanel_touch::ui
