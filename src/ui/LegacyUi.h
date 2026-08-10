#pragma once

#include "core/LegacyConfig.h"
#include "core/NavigationHistory.h"

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
    explicit LegacyUi(const core::LegacyConfig& config);
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
    int screen_width() const;
    int screen_height() const;
    int button_height() const;

    static void action_callback(lv_event_t* event);
    static void deferred_action_callback(void* user_data);

    const core::LegacyConfig& config_;
    core::NavigationHistory navigation_;
    std::vector<std::unique_ptr<Action>> actions_;
    std::vector<std::unique_ptr<Action>> pending_actions_;
    std::string pending_list_id_;
    lv_obj_t* menu_content_{nullptr};
};

}  // namespace micropanel_touch::ui
