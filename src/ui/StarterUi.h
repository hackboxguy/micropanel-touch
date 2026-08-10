#pragma once

#include "core/NavigationHistory.h"
#include "core/StaticIpSettings.h"
#include "core/UiEventQueue.h"
#include "ui/StarterConfig.h"
#include "ui/UiTheme.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
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
              std::function<bool(std::uint64_t)> start_action_demo,
              std::function<void()> cancel_action,
              std::function<void(std::uint64_t)> refresh_action_progress,
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
    void show_wifi_password_demo();
    void show_theme_selection();
    void show_progress_demo();
    void show_action_runner_demo();
    void show_slider_demo();
    void show_placeholder(const std::string& title);
    void show_parent_menu();
    void activate(const std::string& id);
    void queue_action(const std::string& id);
    void clear_screen();
    void create_title(const std::string& title);
    lv_obj_t* create_button(const std::string& title, int y, const std::string& action);
    lv_obj_t* create_button(const std::string& title, int x, int y, int width, int height,
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
    void update_progress_demo();
    void update_action_runner_progress(const core::ActionProgress& progress);
    void show_action_runner_result(const core::ActionResult& result);
    void update_slider_demo();
    void update_wifi_password_length();
    void update_wifi_password_visibility();
    void request_wifi_scan();
    void drain_events();
    int screen_width() const;
    int screen_height() const;
    int button_height() const;

    static void action_callback(lv_event_t* event);
    static void ip_input_callback(lv_event_t* event);
    static void keyboard_callback(lv_event_t* event);
    static void wifi_password_input_callback(lv_event_t* event);
    static void wifi_password_visibility_callback(lv_event_t* event);
    static void wifi_password_keyboard_navigation_callback(lv_event_t* event);
    static void wifi_password_keyboard_callback(lv_event_t* event);
    static void drain_timer_callback(lv_timer_t* timer);
    static void progress_timer_callback(lv_timer_t* timer);
    static void action_progress_timer_callback(lv_timer_t* timer);
    static void slider_callback(lv_event_t* event);
    static void deferred_action_callback(void* user_data);

    StarterConfig config_;
    const UiTheme& theme_;
    core::UiEventQueue& event_queue_;
    std::function<void()> request_wifi_scan_;
    std::function<bool(std::uint64_t)> start_action_demo_;
    std::function<void()> cancel_action_;
    std::function<void(std::uint64_t)> refresh_action_progress_;
    std::function<bool(const std::string&, std::string*)> select_theme_;
    std::function<std::string()> active_theme_name_;
    std::unordered_set<std::string> warned_unsupported_icons_;
    std::vector<std::unique_ptr<Action>> actions_;
    std::vector<std::unique_ptr<PendingAction>> pending_actions_;
    core::NetworkSnapshot network_snapshot_;
    std::optional<core::WifiScanResult> wifi_scan_result_;
    std::string network_text_;
    std::string wifi_text_;
    std::string progress_text_;
    std::string action_runner_status_text_;
    std::string action_runner_log_text_;
    std::string slider_brightness_text_;
    std::string slider_volume_text_;
    std::string wifi_password_length_text_;
    std::string theme_message_;
    core::NavigationHistory navigation_;
    bool network_info_visible_{false};
    bool ip_settings_visible_{false};
    bool wifi_scan_visible_{false};
    bool wifi_password_visible_{false};
    bool wifi_password_uppercase_{false};
    bool action_runner_visible_{false};
    bool action_runner_running_{false};
    std::uint64_t action_runner_job_id_{0};
    std::uint64_t next_action_runner_job_id_{1};
    lv_obj_t* network_label_{nullptr};
    lv_obj_t* menu_content_{nullptr};
    int menu_content_top_{52};
    lv_obj_t* wifi_label_{nullptr};
    lv_obj_t* wifi_spinner_{nullptr};
    lv_obj_t* progress_bar_{nullptr};
    lv_obj_t* progress_label_{nullptr};
    lv_obj_t* action_runner_status_label_{nullptr};
    lv_obj_t* action_runner_log_label_{nullptr};
    lv_obj_t* action_runner_bar_{nullptr};
    lv_obj_t* action_runner_cancel_button_{nullptr};
    lv_obj_t* brightness_slider_{nullptr};
    lv_obj_t* volume_slider_{nullptr};
    lv_obj_t* brightness_slider_label_{nullptr};
    lv_obj_t* volume_slider_label_{nullptr};
    lv_obj_t* wifi_password_input_{nullptr};
    lv_obj_t* wifi_password_length_label_{nullptr};
    lv_obj_t* wifi_password_status_label_{nullptr};
    lv_obj_t* wifi_password_visibility_button_{nullptr};
    lv_obj_t* wifi_password_visibility_icon_{nullptr};
    lv_obj_t* ip_address_input_{nullptr};
    lv_obj_t* prefix_input_{nullptr};
    lv_obj_t* gateway_input_{nullptr};
    lv_obj_t* ip_status_label_{nullptr};
    lv_obj_t* keyboard_{nullptr};
    lv_timer_t* event_timer_{nullptr};
    lv_timer_t* progress_timer_{nullptr};
    lv_timer_t* action_progress_timer_{nullptr};
    std::chrono::steady_clock::time_point progress_started_at_{};
};

}  // namespace micropanel_touch::ui
