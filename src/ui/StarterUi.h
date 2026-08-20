#pragma once

#include "core/NavigationHistory.h"
#include "core/PrivilegedOperations.h"
#include "core/StaticIpSettings.h"
#include "core/UiControl.h"
#include "core/UiEventQueue.h"
#include "platform/SyntheticKeypadInput.h"
#include "platform/SyntheticTouchInput.h"
#include "platform/TouchCalibration.h"
#include "platform/AboutInfo.h"
#include "platform/DisplayBrightnessSettings.h"
#include "platform/ScreenLockSettings.h"
#include "platform/SystemStats.h"
#include "platform/DisplayStandbySettings.h"
#include "ui/StarterConfig.h"
#include "ui/UiTheme.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <lvgl.h>

namespace micropanel_touch::ui {

class StarterUi {
public:
    using FrameCaptureProvider =
        std::function<std::optional<core::UiFrameCapture>(std::string* diagnostic)>;
    using NetworkRequestCallback = std::function<bool(
        std::uint64_t request_id, const core::NetworkOperation& operation,
        std::string* diagnostic)>;
    using SystemUpdateRequestCallback = std::function<bool(
        std::uint64_t request_id, const core::SystemUpdateOperation& operation,
        std::string* diagnostic)>;
    // Asking the release server what it offers. Carries no destination and no
    // key: both are pinned in the image, exactly as for the update itself.
    using SystemUpdateCheckCallback =
        std::function<bool(std::uint64_t request_id, std::string* diagnostic)>;
    using SystemUpdateStatusProvider = std::function<std::string()>;
    using FactoryResetRequestCallback = std::function<bool(std::string* diagnostic)>;
    using TouchCalibrationApplyCallback = std::function<bool(
        const std::vector<platform::TouchCalibrationSample>& samples,
        std::string* diagnostic)>;
    using TouchCalibrationResetCallback = std::function<bool(std::string* diagnostic)>;
    using LogicalToNativePoint = std::function<platform::TouchPoint(platform::TouchPoint)>;
    using DisplayStandbySettingsProvider =
        std::function<std::optional<platform::DisplayStandbySettings>()>;
    using DisplayStandbySettingsApplyCallback = std::function<bool(
        const platform::DisplayStandbySettings& settings, std::string* diagnostic)>;
    using DisplayBrightnessSettingsProvider =
        std::function<std::optional<platform::DisplayBrightnessSettings>()>;
    using DisplayBrightnessPreviewCallback = std::function<bool(
        const platform::DisplayBrightnessSettings& settings, std::string* diagnostic)>;
    using DisplayBrightnessSettingsApplyCallback = std::function<bool(
        const platform::DisplayBrightnessSettings& settings, std::string* diagnostic)>;
    using ScreenLockSettingsProvider =
        std::function<std::optional<platform::ScreenLockSettings>()>;
    using ScreenLockSetPinCallback = std::function<bool(std::string_view pin,
                                                         std::string* diagnostic)>;
    using ScreenLockSetEnabledCallback = std::function<bool(bool enabled,
                                                             std::string* diagnostic)>;
    using ScreenLockVerifyPinCallback = std::function<bool(std::string_view pin)>;
    using ScreenLockSessionCallback = std::function<void(bool locked)>;

    // The base-features milestone adds several seams of the same shape, and
    // this constructor already takes thirty positional parameters. New ones
    // arrive as one named bundle instead, so a reader of a call site can see
    // which lambda is which without counting commas. Each member is optional:
    // an unset one means the panel was built without that capability, and the
    // screen behind it says so rather than showing a plausible zero.
    struct SystemServices {
        std::function<platform::SystemStats()> system_stats;
        std::function<platform::AboutInfo()> about_info;
        // Returns false with a diagnostic if the transition could not even be
        // started. A true return means "scheduled", not "already down".
        std::function<bool(core::PowerAction action, std::string* diagnostic)> request_power;
    };

    StarterUi(StarterConfig config, const UiTheme& theme, core::UiEventQueue& event_queue,
              platform::SyntheticTouchInput* synthetic_touch,
              platform::SyntheticKeypadInput* synthetic_keypad,
              FrameCaptureProvider frame_capture,
              std::function<void()> request_wifi_scan,
              std::function<void()> request_managed_ipv4_profile,
              std::string static_ip_interface,
              NetworkRequestCallback request_network_change,
              SystemUpdateRequestCallback request_system_update,
              SystemUpdateCheckCallback request_system_update_check,
              SystemUpdateStatusProvider system_update_status,
              FactoryResetRequestCallback request_factory_reset,
              std::function<bool(std::uint64_t)> start_action_demo,
              std::function<void()> cancel_action,
              std::function<void(std::uint64_t)> refresh_action_progress,
              std::function<bool(const std::string&, std::string*)> select_theme,
              std::function<std::string()> active_theme_name,
              DisplayStandbySettingsProvider display_standby_settings,
              DisplayStandbySettingsApplyCallback apply_display_standby_settings,
              DisplayBrightnessSettingsProvider display_brightness_settings,
              DisplayBrightnessPreviewCallback preview_display_brightness,
              DisplayBrightnessSettingsApplyCallback apply_display_brightness_settings,
              ScreenLockSettingsProvider screen_lock_settings,
              ScreenLockSetPinCallback set_screen_lock_pin,
              ScreenLockSetEnabledCallback set_screen_lock_enabled,
              ScreenLockVerifyPinCallback verify_screen_lock_pin,
              ScreenLockSessionCallback set_screen_lock_session,
              TouchCalibrationApplyCallback apply_touch_calibration,
              TouchCalibrationResetCallback reset_touch_calibration,
              LogicalToNativePoint logical_to_native_point,
              SystemServices system_services = {});
    ~StarterUi();
    StarterUi(const StarterUi&) = delete;
    StarterUi& operator=(const StarterUi&) = delete;

    void start();
    // Used by display standby just before backlight blanking so wake always
    // resumes at Home rather than an abandoned settings or action page.
    void return_to_home();
    // Network application owns a live result card. Do not clear it merely
    // because the display's inactivity timer happens to expire.
    bool inhibits_display_sleep() const;
    // Shows the lock gate before standby blanking or after a cold start with
    // an enabled lock. This intentionally has no route back to the HMI.
    void show_screen_lock();

private:
    struct Action {
        StarterUi* ui;
        std::string id;
    };

    struct PendingAction {
        StarterUi* ui;
        std::string id;
    };

    struct PendingTapReply {
        StarterUi* ui{};
        std::shared_ptr<std::promise<core::UiControlResponse>> completion;
        lv_timer_t* settlement_timer{nullptr};
    };

    struct PasswordVisibilityControl {
        lv_obj_t* input{nullptr};
        lv_obj_t* button{nullptr};
        lv_obj_t* icon{nullptr};
    };

    void show_root();
    void show_menu(const StarterModule& menu);
    void show_network_info();
    void show_ip_settings();
    void show_network_result(std::string message, bool ok, bool pending);
    void show_system_update();
    void show_system_update_result(std::string message, bool ok, bool pending,
                                   bool offer_update = false);
    void show_wifi();
    void show_wifi_password(std::string ssid, bool secured);
    void submit_wifi_join();
    void submit_wifi_forget();
    void start_network_operation(const core::NetworkOperation& operation,
                                 const std::string& pending_text);
    void show_theme_selection();
    void show_progress_demo();
    void show_action_runner_demo();
    void show_slider_demo();
    void show_display_brightness();
    void show_display_standby();
    void show_factory_reset();
    void submit_factory_reset();
    void show_screen_lock_settings();
    void show_screen_lock_pin_setup();
    void show_screen_lock_disable();
    void show_touch_calibration();
    void show_system_stats();
    void refresh_system_stats();
    void show_about();
    void show_power();
    void submit_power(core::PowerAction action);
    void show_placeholder(const std::string& title);
    void show_parent_menu();
    void activate(const std::string& id);
    void queue_action(const std::string& id);
    void queue_tap(const core::UiControlCommand& command,
                   std::shared_ptr<std::promise<core::UiControlResponse>> completion);
    void queue_text(const core::UiControlCommand& command,
                    std::shared_ptr<std::promise<core::UiControlResponse>> completion);
    core::UiControlResponse handle_control(const core::UiControlCommand& command);
    core::UiControlResponse state_response() const;
    void settle_render() const;
    void append_widget_snapshots(lv_obj_t* object, std::int32_t parent_id,
                                 bool ancestor_redacted, std::uint32_t* next_id,
                                 core::UiControlResponse* response) const;
    void clear_screen();
    void create_title(const std::string& title, int top = 14);
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
    void set_static_ipv4_defaults();
    void set_dhcp_server_defaults();
    void load_managed_ipv4_profile(const core::ManagedIpv4Profile& profile);
    void update_ip_settings_mode();
    void dismiss_keyboard();
    void validate_ip_settings();
    void refresh_network_info();
    void refresh_wifi_scan();
    void update_progress_demo();
    void update_action_runner_progress(const core::ActionProgress& progress);
    void show_action_runner_result(const core::ActionResult& result);
    void update_slider_demo();
    void update_display_brightness_controls();
    void apply_display_brightness_settings();
    void update_display_standby_controls();
    void apply_display_standby_settings();
    void configure_screen_lock_input(lv_obj_t* input, const char* placeholder, int y);
    void configure_password_visibility_control(PasswordVisibilityControl* control, lv_obj_t* input,
                                               int y, int height);
    void create_screen_lock_visibility_control(lv_obj_t* input, int y);
    void focus_screen_lock_input(lv_obj_t* input);
    void submit_screen_lock_pin_setup();
    void submit_screen_lock_unlock();
    void submit_screen_lock_disable();
    void update_wifi_password_length();
    void update_touch_calibration_target();
    void accept_touch_calibration_sample(const core::TouchCalibrationRawSample& sample);
    void reset_touch_calibration();
    void request_wifi_scan();
    void drain_events();
    int screen_width() const;
    int screen_height() const;
    int button_height() const;

    static void action_callback(lv_event_t* event);
    static void ip_input_callback(lv_event_t* event);
    static void ip_mode_callback(lv_event_t* event);
    static void ip_mode_list_draw_callback(lv_event_t* event);
    static void keyboard_callback(lv_event_t* event);
    static void wifi_password_input_callback(lv_event_t* event);
    static void wifi_password_keyboard_navigation_callback(lv_event_t* event);
    static void wifi_password_keyboard_callback(lv_event_t* event);
    static void drain_timer_callback(lv_timer_t* timer);
    static void progress_timer_callback(lv_timer_t* timer);
    static void action_progress_timer_callback(lv_timer_t* timer);
    static void system_stats_timer_callback(lv_timer_t* timer);
    static void slider_callback(lv_event_t* event);
    static void display_brightness_slider_callback(lv_event_t* event);
    static void display_standby_checkbox_callback(lv_event_t* event);
    static void display_standby_slider_callback(lv_event_t* event);
    static void screen_lock_input_callback(lv_event_t* event);
    static void update_password_visibility_control(PasswordVisibilityControl* control);
    static void password_visibility_callback(lv_event_t* event);
    static void screen_lock_keyboard_callback(lv_event_t* event);
    static void deferred_action_callback(void* user_data);
    static void deferred_tap_reply_timer_callback(lv_timer_t* timer);

    StarterConfig config_;
    const UiTheme& theme_;
    core::UiEventQueue& event_queue_;
    platform::SyntheticTouchInput* synthetic_touch_{nullptr};
    platform::SyntheticKeypadInput* synthetic_keypad_{nullptr};
    FrameCaptureProvider frame_capture_;
    std::function<void()> request_wifi_scan_;
    std::function<void()> request_managed_ipv4_profile_;
    std::string static_ip_interface_;
    NetworkRequestCallback request_network_change_;
    SystemUpdateRequestCallback request_system_update_;
    SystemUpdateCheckCallback request_system_update_check_;
    FactoryResetRequestCallback request_factory_reset_;
    SystemUpdateStatusProvider system_update_status_;
    std::function<bool(std::uint64_t)> start_action_demo_;
    std::function<void()> cancel_action_;
    std::function<void(std::uint64_t)> refresh_action_progress_;
    std::function<bool(const std::string&, std::string*)> select_theme_;
    std::function<std::string()> active_theme_name_;
    DisplayStandbySettingsProvider display_standby_settings_provider_;
    DisplayStandbySettingsApplyCallback apply_display_standby_settings_;
    DisplayBrightnessSettingsProvider display_brightness_settings_provider_;
    DisplayBrightnessPreviewCallback preview_display_brightness_;
    DisplayBrightnessSettingsApplyCallback apply_display_brightness_settings_;
    ScreenLockSettingsProvider screen_lock_settings_provider_;
    ScreenLockSetPinCallback set_screen_lock_pin_;
    ScreenLockSetEnabledCallback set_screen_lock_enabled_;
    ScreenLockVerifyPinCallback verify_screen_lock_pin_;
    ScreenLockSessionCallback set_screen_lock_session_;
    TouchCalibrationApplyCallback apply_touch_calibration_;
    TouchCalibrationResetCallback reset_touch_calibration_;
    LogicalToNativePoint logical_to_native_point_;
    std::unordered_set<std::string> warned_unsupported_icons_;
    std::vector<std::unique_ptr<Action>> actions_;
    std::vector<std::unique_ptr<PendingAction>> pending_actions_;
    std::vector<std::unique_ptr<PendingTapReply>> pending_tap_replies_;
    core::NetworkSnapshot network_snapshot_;
    std::optional<core::WifiScanResult> wifi_scan_result_;
    // The network the password screen is collecting a secret for. Held as a
    // copy because the scan result behind it is replaced by the next scan.
    std::string wifi_join_ssid_;
    bool wifi_join_secured_{true};
    // How many networks the current geometry can show without scrolling.
    // Computed from the panel rather than fixed, because the same list has to
    // fit a tall portrait panel and a short landscape one.
    std::size_t wifi_visible_networks_{0U};
    std::string network_text_;
    std::string wifi_text_;
    std::string progress_text_;
    std::string action_runner_status_text_;
    std::string action_runner_log_text_;
    std::string slider_brightness_text_;
    std::string slider_volume_text_;
    std::string display_brightness_label_text_;
    std::string display_brightness_status_text_;
    std::string display_standby_label_text_;
    std::string display_standby_status_text_;
    std::string screen_lock_status_text_;
    // One cached string per stats row. The redraw law is the reason: at 2 Hz
    // an unguarded label rewrite invalidates its area on every tick, and on an
    // SPI panel that is a visible cost for a value that did not change.
    std::vector<lv_obj_t*> system_stats_value_labels_;
    std::vector<std::string> system_stats_value_text_;
    SystemServices system_services_;
    lv_obj_t* power_status_label_{nullptr};
    lv_obj_t* power_reboot_button_{nullptr};
    lv_obj_t* power_shutdown_button_{nullptr};
    // Which action, if any, is one press away. Arming is per-action rather
    // than a single flag: arming Restart and then pressing Shut down must not
    // shut the panel down.
    std::optional<core::PowerAction> power_armed_;
    std::string wifi_password_length_text_;
    std::string theme_message_;
    core::NavigationHistory navigation_;
    std::string screen_id_{"root"};
    bool network_info_visible_{false};
    bool ip_settings_visible_{false};
    bool ip_settings_profile_loaded_{false};
    bool network_result_visible_{false};
    bool network_apply_pending_{false};
    bool dhcp_server_apply_confirmed_{false};
    std::uint64_t network_apply_request_id_{0};
    std::uint64_t next_network_apply_request_id_{1};
    bool system_update_visible_{false};
    bool system_update_result_visible_{false};
    bool system_update_pending_{false};
    std::uint64_t system_update_request_id_{0};
    std::uint64_t next_system_update_request_id_{1};
    // Set only by a check that came back `available`; cleared whenever the
    // update screen is rebuilt, so a stale offer can never arm an install.
    bool system_update_offer_available_{false};
    std::string system_update_offer_version_;
    bool wifi_scan_visible_{false};
    bool wifi_password_visible_{false};
    bool wifi_password_uppercase_{false};
    bool action_runner_visible_{false};
    bool action_runner_running_{false};
    std::uint64_t action_runner_job_id_{0};
    std::uint64_t next_action_runner_job_id_{1};
    bool touch_calibration_visible_{false};
    bool system_stats_visible_{false};
    bool power_visible_{false};
    bool touch_calibration_complete_{false};
    bool touch_calibration_reset_confirmed_{false};
    bool display_standby_available_{false};
    bool display_brightness_available_{false};
    bool factory_reset_visible_{false};
    bool factory_reset_confirmed_{false};
    lv_obj_t* factory_reset_pin_input_{nullptr};
    lv_obj_t* factory_reset_status_label_{nullptr};
    lv_obj_t* factory_reset_button_{nullptr};
    bool screen_lock_settings_visible_{false};
    bool screen_lock_visible_{false};
    bool screen_lock_pin_setup_visible_{false};
    bool screen_lock_disable_visible_{false};
    bool screen_lock_available_{false};
    platform::ScreenLockAttemptLimiter screen_lock_attempt_limiter_;
    platform::DisplayBrightnessSettings display_brightness_settings_;
    platform::DisplayBrightnessSettings applied_display_brightness_settings_;
    platform::DisplayStandbySettings display_standby_settings_;
    platform::DisplayStandbySettings applied_display_standby_settings_;
    std::size_t touch_calibration_target_index_{0U};
    std::vector<platform::TouchPoint> touch_calibration_targets_;
    std::vector<platform::TouchCalibrationSample> touch_calibration_samples_;
    lv_obj_t* network_label_{nullptr};
    lv_obj_t* menu_content_{nullptr};
    int menu_content_top_{52};
    lv_obj_t* wifi_label_{nullptr};
    std::vector<lv_obj_t*> wifi_network_rows_;
    // What the rendered rows currently show. Rebuilding them when it has not
    // changed destroys the button under a finger mid-press; see
    // refresh_wifi_scan().
    std::string wifi_rows_signature_;
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
    lv_obj_t* display_brightness_slider_{nullptr};
    lv_obj_t* display_brightness_label_{nullptr};
    lv_obj_t* display_brightness_status_label_{nullptr};
    lv_obj_t* display_standby_checkbox_{nullptr};
    lv_obj_t* display_standby_slider_{nullptr};
    lv_obj_t* display_standby_label_{nullptr};
    lv_obj_t* display_standby_status_label_{nullptr};
    lv_obj_t* display_standby_apply_button_{nullptr};
    lv_obj_t* screen_lock_status_label_{nullptr};
    lv_obj_t* screen_lock_pin_input_{nullptr};
    lv_obj_t* screen_lock_pin_confirm_input_{nullptr};
    std::vector<std::unique_ptr<PasswordVisibilityControl>> screen_lock_visibility_controls_;
    lv_obj_t* screen_lock_keyboard_{nullptr};
    lv_obj_t* wifi_password_input_{nullptr};
    lv_obj_t* wifi_password_length_label_{nullptr};
    lv_obj_t* wifi_password_status_label_{nullptr};
    std::unique_ptr<PasswordVisibilityControl> wifi_password_visibility_control_;
    lv_obj_t* ip_mode_dropdown_{nullptr};
    lv_obj_t* ip_address_label_{nullptr};
    lv_obj_t* ip_address_input_{nullptr};
    lv_obj_t* gateway_label_{nullptr};
    lv_obj_t* gateway_input_{nullptr};
    lv_obj_t* netmask_label_{nullptr};
    lv_obj_t* netmask_input_{nullptr};
    lv_obj_t* lease_end_label_{nullptr};
    lv_obj_t* lease_end_input_{nullptr};
    lv_obj_t* ip_status_label_{nullptr};
    lv_obj_t* network_result_label_{nullptr};
    lv_obj_t* system_update_label_{nullptr};
    lv_obj_t* system_update_result_label_{nullptr};
    lv_obj_t* ip_apply_button_{nullptr};
    lv_obj_t* ip_back_button_{nullptr};
    lv_obj_t* keyboard_{nullptr};
    lv_obj_t* touch_calibration_status_label_{nullptr};
    lv_obj_t* touch_calibration_target_{nullptr};
    lv_obj_t* touch_calibration_reset_button_{nullptr};
    lv_obj_t* touch_calibration_cancel_button_{nullptr};
    lv_timer_t* event_timer_{nullptr};
    lv_timer_t* progress_timer_{nullptr};
    lv_timer_t* action_progress_timer_{nullptr};
    lv_timer_t* system_stats_timer_{nullptr};
    std::chrono::steady_clock::time_point progress_started_at_{};
};

}  // namespace micropanel_touch::ui
