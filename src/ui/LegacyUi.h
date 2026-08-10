#pragma once

#include "core/LegacyConfig.h"
#include "core/NavigationHistory.h"
#include "core/UiControl.h"
#include "core/UiEventQueue.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <lvgl.h>

namespace micropanel_touch::ui {

// A deliberately read-only renderer for the currently supported legacy
// surface. It provides the real config-driven navigation spine before any
// legacy command is admitted to ActionCompiler's allowlist.
class LegacyUi {
public:
    LegacyUi(const core::LegacyConfig& config, core::UiEventQueue& event_queue);
    ~LegacyUi();
    LegacyUi(const LegacyUi&) = delete;
    LegacyUi& operator=(const LegacyUi&) = delete;

    void start();

private:
    enum class ActionKind {
        Module,
        Back,
        ListItem,
    };

    struct Action {
        LegacyUi* ui{};
        ActionKind kind{ActionKind::Module};
        std::string module_id;
        std::size_t list_index{};
    };

    void clear_screen();
    void create_title(const std::string& title);
    void create_menu_content(int top = 52);
    void create_menu_button(const std::string& title, ActionKind kind,
                            const std::string& module_id = {}, std::size_t list_index = 0U);
    void show_root();
    void show_menu(const core::LegacyModule& menu);
    void show_generic_list(const core::LegacyModule& list);
    void show_not_implemented(const core::LegacyModule& module);
    void show_list_item_pending(const core::LegacyModule& list, std::size_t list_index);
    void show_parent_menu();
    void activate(const Action& action);
    void queue_action(const Action& action);
    core::UiControlResponse handle_control(const core::UiControlCommand& command);
    core::UiControlResponse state_response() const;
    void settle_render() const;
    void append_widget_snapshots(lv_obj_t* object, std::int32_t parent_id,
                                 bool ancestor_redacted, std::uint32_t* next_id,
                                 core::UiControlResponse* response) const;
    std::vector<std::string> path_to_module(const std::string& target) const;
    bool activate_current_target(const std::string& target, std::string* diagnostic);
    int screen_width() const;
    int screen_height() const;
    int button_height() const;

    static void action_callback(lv_event_t* event);
    static void deferred_action_callback(void* user_data);
    static void control_timer_callback(lv_timer_t* timer);

    const core::LegacyConfig& config_;
    core::UiEventQueue& event_queue_;
    core::NavigationHistory navigation_;
    std::vector<std::unique_ptr<Action>> actions_;
    std::vector<std::unique_ptr<Action>> pending_actions_;
    std::string pending_list_id_;
    std::string screen_id_{"root"};
    lv_obj_t* menu_content_{nullptr};
    lv_timer_t* control_timer_{nullptr};
};

}  // namespace micropanel_touch::ui
