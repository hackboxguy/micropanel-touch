#pragma once

#include "core/NavigationHistory.h"
#include "core/StaticIpSettings.h"
#include "core/UiEventQueue.h"
#include "ui/StarterConfig.h"
#include "ui/UiTheme.h"

#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <lvgl.h>

namespace micropanel_touch::ui {

class StarterUi {
public:
    StarterUi(StarterConfig config, const UiTheme& theme, core::UiEventQueue& event_queue,
              std::function<void()> request_wifi_scan,
              std::function<bool(const std::string&, std::string*)> select_theme,
              std::function<std::string()> active_theme_name);
    ~StarterUi();
    StarterUi(const StarterUi&) = delete;
    StarterUi& operator=(const StarterUi&) = delete;

    void start();

private:
    struct Action {
        StarterUi* ui;
        std::string id;
    };

    struct PendingAction {
        StarterUi* ui;
        std::string id;
    };

    void show_root();
    void show_menu(const StarterModule& menu);
    void show_network_info();
    void show_ip_settings();
    void show_wifi();
    void show_theme_selection();
    void show_placeholder(const std::string& title);
    void show_parent_menu();
    void activate(const std::string& id);
    void queue_action(const std::string& id);
    void clear_screen();
    void create_title(const std::string& title);
    void create_button(const std::string& title, int y, const std::string& action);
    void create_button(const std::string& title, int x, int y, int width, int height,
                       const std::string& action);
    void create_menu_content(const StarterMenuPresentation& presentation, int top = 52);
    void create_menu_button(const std::string& title, const std::string& icon,
                            const std::string& color, const std::string& action,
                            const StarterMenuPresentation& presentation);
    void create_ip_input(const char* placeholder, int y, int height,
                         const char* accepted_characters,
                         lv_obj_t** input);
    void focus_ip_input(lv_obj_t* input);
    void dismiss_keyboard();
    void validate_ip_settings();
    void refresh_network_info();
    void refresh_wifi_scan();
    void request_wifi_scan();
    void drain_events();
    int screen_width() const;
    int screen_height() const;
    int button_height() const;

    static void action_callback(lv_event_t* event);
    static void ip_input_callback(lv_event_t* event);
    static void keyboard_callback(lv_event_t* event);
    static void drain_timer_callback(lv_timer_t* timer);
    static void deferred_action_callback(void* user_data);

    StarterConfig config_;
    const UiTheme& theme_;
    core::UiEventQueue& event_queue_;
    std::function<void()> request_wifi_scan_;
    std::function<bool(const std::string&, std::string*)> select_theme_;
    std::function<std::string()> active_theme_name_;
    std::unordered_set<std::string> warned_unsupported_icons_;
    std::vector<std::unique_ptr<Action>> actions_;
    std::vector<std::unique_ptr<PendingAction>> pending_actions_;
    core::NetworkSnapshot network_snapshot_;
    std::optional<core::WifiScanResult> wifi_scan_result_;
    std::string network_text_;
    std::string wifi_text_;
    std::string theme_message_;
    core::NavigationHistory navigation_;
    bool network_info_visible_{false};
    bool ip_settings_visible_{false};
    bool wifi_scan_visible_{false};
    lv_obj_t* network_label_{nullptr};
    lv_obj_t* menu_content_{nullptr};
    int menu_content_top_{52};
    lv_obj_t* wifi_label_{nullptr};
    lv_obj_t* wifi_spinner_{nullptr};
    lv_obj_t* ip_address_input_{nullptr};
    lv_obj_t* prefix_input_{nullptr};
    lv_obj_t* gateway_input_{nullptr};
    lv_obj_t* ip_status_label_{nullptr};
    lv_obj_t* keyboard_{nullptr};
    lv_timer_t* event_timer_{nullptr};
};

}  // namespace micropanel_touch::ui
