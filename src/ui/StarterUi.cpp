#include "ui/StarterUi.h"

#include "ui/BuiltinIcon.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace micropanel_touch::ui {
namespace {

constexpr int kHorizontalMargin = 16;
constexpr int kMenuBottomMargin = 12;
constexpr int kMenuGap = 8;
constexpr auto kProgressDemoDuration = std::chrono::seconds(30);
constexpr std::uint32_t kProgressDemoPeriodMs = 200U;
constexpr std::uint32_t kActionProgressPeriodMs = 250U;
constexpr int kSliderTrackThickness = 8;
constexpr int kSliderHitThickness = 40;
constexpr int kSliderHitPadding = (kSliderHitThickness - kSliderTrackThickness) / 2;
constexpr int kTouchCalibrationTargetDiameter = 72;
constexpr int kTouchCalibrationTargetAcceptRadius = 64;
constexpr std::size_t kMaximumWidgetSnapshots = 256U;
constexpr std::size_t kMaximumWidgetTextBytes = 256U;

constexpr lv_buttonmatrix_ctrl_t kPasswordKey =
    static_cast<lv_buttonmatrix_ctrl_t>(LV_BUTTONMATRIX_CTRL_POPOVER | 1);
constexpr lv_buttonmatrix_ctrl_t kPasswordWideKey =
    static_cast<lv_buttonmatrix_ctrl_t>(LV_BUTTONMATRIX_CTRL_POPOVER | 2);
constexpr lv_buttonmatrix_ctrl_t kPasswordControlKey = static_cast<lv_buttonmatrix_ctrl_t>(
    LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | 1);
constexpr lv_buttonmatrix_ctrl_t kPasswordWideControlKey = static_cast<lv_buttonmatrix_ctrl_t>(
    LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | 2);
// Backspace is deliberately repeat-enabled: one delete on press, then repeats
// while held, matching physical keypad behaviour.
constexpr lv_buttonmatrix_ctrl_t kPasswordBackspaceKey = static_cast<lv_buttonmatrix_ctrl_t>(
    LV_BUTTONMATRIX_CTRL_CLICK_TRIG | 1);

// A conventional keyboard puts 10–12 keys across a 320 px portrait panel.
// These three five-column pages trade page changes for practical 50+ px letter
// keys and a dedicated number/punctuation page. The event preprocessor below
// handles page controls before LVGL can insert their labels into the password.
const char* const kPasswordKeyboardLowerPageOne[] = {
    "q", "w", "e", "r", "t", "\n",
    "a", "s", "d", "f", "g", "\n",
    "z", "x", "c", "v", "b", "\n",
    "Next", "ABC", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, "",
};
const lv_buttonmatrix_ctrl_t kPasswordKeyboardLowerPageOneControls[] = {
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordWideControlKey, kPasswordControlKey, kPasswordWideKey,
    kPasswordBackspaceKey, kPasswordControlKey,
};

const char* const kPasswordKeyboardLowerPageTwo[] = {
    "y", "u", "i", "o", "p", "\n",
    "h", "j", "k", "l", "m", "\n",
    "n", "0", "1", "2", "3", "\n",
    "Prev", "Next", "ABC", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, "",
};
const lv_buttonmatrix_ctrl_t kPasswordKeyboardLowerPageTwoControls[] = {
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordControlKey, kPasswordControlKey, kPasswordControlKey,
    kPasswordWideKey, kPasswordBackspaceKey, kPasswordControlKey,
};

const char* const kPasswordKeyboardUpperPageOne[] = {
    "Q", "W", "E", "R", "T", "\n",
    "A", "S", "D", "F", "G", "\n",
    "Z", "X", "C", "V", "B", "\n",
    "Next", "abc", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, "",
};
const lv_buttonmatrix_ctrl_t kPasswordKeyboardUpperPageOneControls[] = {
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordWideControlKey, kPasswordControlKey, kPasswordWideKey,
    kPasswordBackspaceKey, kPasswordControlKey,
};

const char* const kPasswordKeyboardUpperPageTwo[] = {
    "Y", "U", "I", "O", "P", "\n",
    "H", "J", "K", "L", "M", "\n",
    "N", "0", "1", "2", "3", "\n",
    "Prev", "Next", "abc", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, "",
};
const lv_buttonmatrix_ctrl_t kPasswordKeyboardUpperPageTwoControls[] = {
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordControlKey, kPasswordControlKey, kPasswordControlKey,
    kPasswordWideKey, kPasswordBackspaceKey, kPasswordControlKey,
};

const char* const kPasswordKeyboardSymbols[] = {
    "4", "5", "6", "7", "8", "\n",
    "9", "!", "@", "#", "$", "\n",
    "%", "^", "&", "*", "(", "\n",
    ")", ",", ".", "-", "_", "\n",
    "+", "=", "?", "/", ":", "\n",
    "Prev", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, "",
};
const lv_buttonmatrix_ctrl_t kPasswordKeyboardSymbolsControls[] = {
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey, kPasswordKey,
    kPasswordWideControlKey, kPasswordWideKey, kPasswordBackspaceKey,
    kPasswordControlKey,
};

void configure_demo_slider_interaction(lv_obj_t* slider) {
    // The actual slider object is the 8 px rail. Enlarging its knob and click
    // area keeps the calculation geometry and what the user sees identical:
    // a finger's position maps directly to the knob center and value.
    lv_obj_set_style_pad_all(slider, kSliderHitPadding, LV_PART_KNOB);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_ext_click_area(slider, kSliderHitPadding);
}

const char* widget_type(const lv_obj_t* object) {
    if (lv_obj_get_parent(object) == nullptr) {
        return "screen";
    }
    if (lv_obj_check_type(object, &lv_textarea_class)) {
        return "textarea";
    }
    if (lv_obj_check_type(object, &lv_label_class)) {
        return "label";
    }
    if (lv_obj_check_type(object, &lv_button_class)) {
        return "button";
    }
    if (lv_obj_check_type(object, &lv_keyboard_class)) {
        return "keyboard";
    }
    if (lv_obj_check_type(object, &lv_slider_class)) {
        return "slider";
    }
    if (lv_obj_check_type(object, &lv_bar_class)) {
        return "bar";
    }
    return "object";
}

std::string bounded_text(const char* text, bool* truncated) {
    if (text == nullptr) {
        return {};
    }
    std::string result(text);
    if (result.size() > kMaximumWidgetTextBytes) {
        result.resize(kMaximumWidgetTextBytes);
        *truncated = true;
    }
    return result;
}

std::optional<std::string> dotted_netmask_from_prefix(std::string_view prefix_text) {
    unsigned int prefix = 0U;
    const auto parsed = std::from_chars(prefix_text.data(), prefix_text.data() + prefix_text.size(),
                                        prefix);
    if (parsed.ec != std::errc{} || parsed.ptr != prefix_text.data() + prefix_text.size() ||
        prefix > 32U) {
        return std::nullopt;
    }
    const std::uint32_t mask = prefix == 0U ? 0U : ~std::uint32_t{0} << (32U - prefix);
    return std::to_string((mask >> 24U) & 0xffU) + "." +
           std::to_string((mask >> 16U) & 0xffU) + "." +
           std::to_string((mask >> 8U) & 0xffU) + "." +
           std::to_string(mask & 0xffU);
}

}  // namespace

StarterUi::StarterUi(StarterConfig config, const UiTheme& theme, core::UiEventQueue& event_queue,
                     platform::SyntheticTouchInput* synthetic_touch,
                     platform::SyntheticKeypadInput* synthetic_keypad,
                     FrameCaptureProvider frame_capture,
                     std::function<void()> request_wifi_scan,
                     std::function<void()> request_managed_ipv4_profile,
                     std::string static_ip_interface,
                     NetworkRequestCallback request_network_change,
                     SystemUpdateRequestCallback request_system_update,
                     SystemUpdateStatusProvider system_update_status,
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
                     LogicalToNativePoint logical_to_native_point)
    : config_(std::move(config)), theme_(theme), event_queue_(event_queue),
      synthetic_touch_(synthetic_touch), synthetic_keypad_(synthetic_keypad),
      frame_capture_(std::move(frame_capture)),
      request_wifi_scan_(std::move(request_wifi_scan)),
      request_managed_ipv4_profile_(std::move(request_managed_ipv4_profile)),
      static_ip_interface_(std::move(static_ip_interface)),
      request_network_change_(std::move(request_network_change)),
      request_system_update_(std::move(request_system_update)),
      system_update_status_(std::move(system_update_status)),
      start_action_demo_(std::move(start_action_demo)), cancel_action_(std::move(cancel_action)),
      refresh_action_progress_(std::move(refresh_action_progress)),
      select_theme_(std::move(select_theme)),
      active_theme_name_(std::move(active_theme_name)),
      display_standby_settings_provider_(std::move(display_standby_settings)),
      apply_display_standby_settings_(std::move(apply_display_standby_settings)),
      display_brightness_settings_provider_(std::move(display_brightness_settings)),
      preview_display_brightness_(std::move(preview_display_brightness)),
      apply_display_brightness_settings_(std::move(apply_display_brightness_settings)),
      screen_lock_settings_provider_(std::move(screen_lock_settings)),
      set_screen_lock_pin_(std::move(set_screen_lock_pin)),
      set_screen_lock_enabled_(std::move(set_screen_lock_enabled)),
      verify_screen_lock_pin_(std::move(verify_screen_lock_pin)),
      set_screen_lock_session_(std::move(set_screen_lock_session)),
      apply_touch_calibration_(std::move(apply_touch_calibration)),
      reset_touch_calibration_(std::move(reset_touch_calibration)),
      logical_to_native_point_(std::move(logical_to_native_point)) {}

StarterUi::~StarterUi() {
    for (const auto& action : pending_actions_) {
        lv_async_call_cancel(deferred_action_callback, action.get());
    }
    for (const auto& reply : pending_tap_replies_) {
        if (reply->settlement_timer != nullptr) {
            lv_timer_delete(reply->settlement_timer);
        }
        if (reply->completion != nullptr) {
            try {
                reply->completion->set_value(
                    {false, {}, {}, {}, false, "UI stopped before synthetic tap settled"});
            } catch (const std::future_error&) {
                // The control peer already disconnected.
            }
        }
    }
    if (event_timer_ != nullptr) {
        lv_timer_delete(event_timer_);
    }
    if (progress_timer_ != nullptr) {
        lv_timer_delete(progress_timer_);
    }
    if (action_progress_timer_ != nullptr) {
        lv_timer_delete(action_progress_timer_);
    }
}

void StarterUi::start() {
    event_timer_ = lv_timer_create(drain_timer_callback, 50, this);
    show_root();
    // The fbdev driver creates the LVGL screen before it discovers the real
    // framebuffer geometry. Resolve the root tree after that resize instead
    // of relying on the first input event to trigger its deferred layout.
    lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());
}

void StarterUi::return_to_home() {
    show_root();
    lv_obj_update_layout(lv_screen_active());
}

bool StarterUi::inhibits_display_sleep() const {
    return network_apply_pending_ || system_update_pending_;
}

void StarterUi::clear_screen() {
    if (progress_timer_ != nullptr) {
        lv_timer_delete(progress_timer_);
        progress_timer_ = nullptr;
    }
    if (action_progress_timer_ != nullptr) {
        lv_timer_delete(action_progress_timer_);
        action_progress_timer_ = nullptr;
    }
    // The starter UI never copies a Wi-Fi password outside LVGL. Clear the
    // widget before its screen is torn down so it cannot survive a navigation
    // event in a still-renderable text area.
    if (wifi_password_input_ != nullptr) {
        lv_textarea_set_text(wifi_password_input_, "");
    }
    // PINs must not outlive their lock screen.  In particular, do this before
    // lv_obj_clean() so a secret never survives a navigation redraw.
    if (screen_lock_pin_input_ != nullptr) {
        lv_textarea_set_text(screen_lock_pin_input_, "");
    }
    if (screen_lock_pin_confirm_input_ != nullptr) {
        lv_textarea_set_text(screen_lock_pin_confirm_input_, "");
    }
    lv_obj_t* const screen = lv_screen_active();
    lv_obj_clean(screen);
    actions_.clear();
    menu_content_ = nullptr;
    network_label_ = nullptr;
    network_info_visible_ = false;
    network_text_.clear();
    wifi_label_ = nullptr;
    wifi_spinner_ = nullptr;
    wifi_scan_visible_ = false;
    wifi_text_.clear();
    progress_bar_ = nullptr;
    progress_label_ = nullptr;
    progress_text_.clear();
    action_runner_visible_ = false;
    action_runner_running_ = false;
    action_runner_status_label_ = nullptr;
    action_runner_log_label_ = nullptr;
    action_runner_bar_ = nullptr;
    action_runner_cancel_button_ = nullptr;
    action_runner_status_text_.clear();
    action_runner_log_text_.clear();
    touch_calibration_visible_ = false;
    touch_calibration_complete_ = false;
    touch_calibration_reset_confirmed_ = false;
    touch_calibration_target_index_ = 0U;
    touch_calibration_targets_.clear();
    touch_calibration_samples_.clear();
    brightness_slider_ = nullptr;
    volume_slider_ = nullptr;
    brightness_slider_label_ = nullptr;
    volume_slider_label_ = nullptr;
    slider_brightness_text_.clear();
    slider_volume_text_.clear();
    display_brightness_slider_ = nullptr;
    display_brightness_label_ = nullptr;
    display_brightness_status_label_ = nullptr;
    display_brightness_label_text_.clear();
    display_brightness_status_text_.clear();
    display_brightness_available_ = false;
    display_standby_checkbox_ = nullptr;
    display_standby_slider_ = nullptr;
    display_standby_label_ = nullptr;
    display_standby_status_label_ = nullptr;
    display_standby_apply_button_ = nullptr;
    display_standby_label_text_.clear();
    display_standby_status_text_.clear();
    display_standby_available_ = false;
    screen_lock_status_label_ = nullptr;
    screen_lock_pin_input_ = nullptr;
    screen_lock_pin_confirm_input_ = nullptr;
    screen_lock_visibility_controls_.clear();
    screen_lock_keyboard_ = nullptr;
    screen_lock_status_text_.clear();
    screen_lock_settings_visible_ = false;
    screen_lock_visible_ = false;
    screen_lock_pin_setup_visible_ = false;
    screen_lock_disable_visible_ = false;
    screen_lock_available_ = false;
    wifi_password_visible_ = false;
    wifi_password_uppercase_ = false;
    wifi_password_input_ = nullptr;
    wifi_password_length_label_ = nullptr;
    wifi_password_status_label_ = nullptr;
    wifi_password_visibility_control_.reset();
    wifi_password_length_text_.clear();
    ip_settings_visible_ = false;
    ip_settings_profile_loaded_ = false;
    network_result_visible_ = false;
    network_apply_pending_ = false;
    dhcp_server_apply_confirmed_ = false;
    network_apply_request_id_ = 0U;
    system_update_visible_ = false;
    system_update_result_visible_ = false;
    system_update_pending_ = false;
    system_update_request_id_ = 0U;
    ip_mode_dropdown_ = nullptr;
    ip_address_label_ = nullptr;
    ip_address_input_ = nullptr;
    gateway_label_ = nullptr;
    gateway_input_ = nullptr;
    netmask_label_ = nullptr;
    netmask_input_ = nullptr;
    lease_end_label_ = nullptr;
    lease_end_input_ = nullptr;
    ip_status_label_ = nullptr;
    network_result_label_ = nullptr;
    system_update_label_ = nullptr;
    system_update_result_label_ = nullptr;
    ip_apply_button_ = nullptr;
    ip_back_button_ = nullptr;
    keyboard_ = nullptr;
    touch_calibration_status_label_ = nullptr;
    touch_calibration_target_ = nullptr;
    touch_calibration_reset_button_ = nullptr;
    touch_calibration_cancel_button_ = nullptr;
}

int StarterUi::screen_width() const {
    return lv_display_get_horizontal_resolution(nullptr);
}

int StarterUi::screen_height() const {
    return lv_display_get_vertical_resolution(nullptr);
}

int StarterUi::button_height() const {
    return screen_height() > screen_width() ? 48 : 44;
}

void StarterUi::create_title(const std::string& title, int top) {
    lv_obj_t* const label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, title.c_str());
    UiTheme::set_role(label, UiThemeRole::Title);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, top);
}

lv_obj_t* StarterUi::create_button(const std::string& title, int y, const std::string& action) {
    const int width = screen_width() - 2 * kHorizontalMargin;
    return create_button(title, (screen_width() - width) / 2, y, width, button_height(), action);
}

lv_obj_t* StarterUi::create_button(const std::string& title, int x, int y, int width, int height,
                                   const std::string& action) {
    lv_obj_t* const button = lv_button_create(lv_screen_active());
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);

    auto callback = std::make_unique<Action>(Action{this, action});
    lv_obj_add_event_cb(button, action_callback, LV_EVENT_CLICKED, callback.get());
    actions_.push_back(std::move(callback));

    lv_obj_t* const label = lv_label_create(button);
    std::string button_text;
    if (action == "__back") {
        button_text = builtin_icon_symbol("back");
        button_text += "  ";
    }
    button_text += title;
    lv_label_set_text(label, button_text.c_str());
    lv_obj_center(label);
    return button;
}

void StarterUi::create_menu_content(const StarterMenuPresentation& presentation, int top) {
    menu_content_ = lv_obj_create(lv_screen_active());
    menu_content_top_ = top;
    lv_obj_set_size(menu_content_, screen_width() - 2 * kHorizontalMargin,
                    screen_height() - top - kMenuBottomMargin);
    lv_obj_align(menu_content_, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_style_bg_opa(menu_content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_content_, 0, 0);
    lv_obj_set_style_pad_all(menu_content_, 0, 0);
    lv_obj_set_style_pad_row(menu_content_, kMenuGap, 0);
    lv_obj_set_style_pad_column(menu_content_, kMenuGap, 0);
    lv_obj_set_scrollbar_mode(menu_content_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(menu_content_, LV_DIR_VER);
    lv_obj_set_flex_flow(menu_content_, presentation.layout == StarterMenuLayout::Grid
                                             ? LV_FLEX_FLOW_ROW_WRAP
                                             : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
}

void StarterUi::create_menu_button(const std::string& title, const std::string& icon,
                                   const std::string& color, const std::string& action,
                                   const StarterMenuPresentation& presentation) {
    if (menu_content_ == nullptr) {
        return;
    }

    const bool grid = presentation.layout == StarterMenuLayout::Grid;
    // Flex resolves its content box during the next layout pass. Use the same
    // screen-derived dimensions used to create this container so its first
    // children have a real size before that pass.
    const int content_width = screen_width() - 2 * kHorizontalMargin;
    const int content_height = screen_height() - menu_content_top_ - kMenuBottomMargin;
    const unsigned int columns = std::max(1U, presentation.columns);
    const unsigned int rows = std::max(1U, presentation.rows);
    const int width = grid
                          ? (content_width - static_cast<int>(columns - 1U) * kMenuGap) /
                                static_cast<int>(columns)
                          : content_width;
    const int grid_height =
        (content_height - static_cast<int>(rows - 1U) * kMenuGap) / static_cast<int>(rows);
    const int tile_height = std::max(button_height(), std::min(width, grid_height));

    lv_obj_t* const button = lv_button_create(menu_content_);
    lv_obj_set_size(button, width, grid ? tile_height : button_height());
    if (!color.empty()) {
        const lv_color_t accent = UiTheme::color_from_hex(color);
        lv_obj_set_style_bg_color(button, accent, 0);
        // Local default styles take precedence over the theme, so give every
        // config-colored button its own visible pressed feedback as well.
        lv_obj_set_style_bg_color(button, lv_color_darken(accent, LV_OPA_20), LV_STATE_PRESSED);
    }
    if (grid) {
        theme_.apply_tile_variant(button);
    }

    auto callback = std::make_unique<Action>(Action{this, action});
    lv_obj_add_event_cb(button, action_callback, LV_EVENT_CLICKED, callback.get());
    actions_.push_back(std::move(callback));

    const std::string resolved_icon = icon.empty() && action == "__back"
        ? std::string("back")
        : icon;
    const char* const symbol = builtin_icon_symbol(resolved_icon);
    if (!icon.empty() && symbol == nullptr && warned_unsupported_icons_.insert(icon).second) {
        std::cerr << "Icon '" << icon
                  << "' is accepted but not yet supported; named built-in symbols are available, "
                     "while image paths wait for PNG support.\n";
    }
    std::string button_text;
    if (symbol != nullptr) {
        button_text = symbol;
        button_text += grid ? "\n" : "  ";
    }
    button_text += title;

    lv_obj_t* const label = lv_label_create(button);
    lv_label_set_text(label, button_text.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void StarterUi::show_root() {
    clear_screen();
    screen_id_ = "root";
    navigation_.reset();
    create_title("MicroPanel Touch");
    const StarterMenuPresentation& presentation = config_.root_presentation();
    create_menu_content(presentation);
    for (const StarterModule* module : config_.root_menus()) {
        create_menu_button(module->title, module->icon, module->presentation.accent, module->id,
                           presentation);
    }
}

void StarterUi::show_menu(const StarterModule& menu) {
    clear_screen();
    screen_id_ = menu.id;
    create_title(menu.title);
    create_menu_content(menu.presentation);
    for (const auto& item : menu.submenus) {
        create_menu_button(item.title, item.icon,
                           item.color.empty() ? menu.presentation.accent : item.color,
                           item.id == "back" ? "__back" : item.id, menu.presentation);
    }
}

void StarterUi::show_network_info() {
    clear_screen();
    screen_id_ = "netinfo";
    network_info_visible_ = true;
    create_title("Network Info");

    network_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(network_label_, screen_width() - 2 * kHorizontalMargin);
    lv_label_set_long_mode(network_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(network_label_, LV_ALIGN_TOP_MID, 0, 52);
    UiTheme::set_role(network_label_, UiThemeRole::DimText);
    create_button("Back", screen_height() - button_height() - 12,
                  "__back");
    refresh_network_info();
}

void StarterUi::create_ip_input(const char* placeholder, int y, int height,
                                const char* accepted_characters, lv_obj_t** input) {
    *input = lv_textarea_create(lv_screen_active());
    lv_textarea_set_one_line(*input, true);
    lv_textarea_set_placeholder_text(*input, placeholder);
    lv_textarea_set_accepted_chars(*input, accepted_characters);
    // lv_textarea_set_one_line() resizes the control to its content height.
    // Apply the touch-target geometry afterwards so it cannot be collapsed.
    lv_obj_set_size(*input, screen_width() - 2 * kHorizontalMargin, height);
    lv_obj_align(*input, LV_ALIGN_TOP_MID, 0, y);
    // This screen owns a fixed layout. A focused textarea must not pan its
    // parent screen (and therefore the mode selector or keyboard) merely to
    // make an already-visible numeric field visible.
    lv_obj_remove_flag(*input, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_textarea_set_cursor_click_pos(*input, true);
    lv_obj_add_event_cb(*input, ip_input_callback, LV_EVENT_CLICKED, this);
}

void StarterUi::show_ip_settings() {
    clear_screen();
    screen_id_ = "netsettings";
    ip_settings_visible_ = true;
    const bool portrait = screen_height() > screen_width();
    // The portrait server form needs room for four values and its numeric
    // keypad. Keep the familiar title styling but use the compact top band on
    // this screen only, rather than shrinking the text or the field targets.
    create_title("IP Settings", portrait ? 6 : 14);

    ip_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(ip_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_label_set_long_mode(ip_status_label_, LV_LABEL_LONG_WRAP);
    const bool applying_enabled = static_cast<bool>(request_network_change_);
    const std::string introduction = applying_enabled
        ? "Choose a network mode for " + static_ip_interface_ + "."
        : "Choose a network mode; no network changes.";
    lv_label_set_text(ip_status_label_, introduction.c_str());
    lv_obj_align(ip_status_label_, LV_ALIGN_TOP_MID, 0, portrait ? 30 : 36);
    UiTheme::set_role(ip_status_label_, UiThemeRole::DimText);

    const int mode_y = portrait ? 50 : 54;
    const int input_y = portrait ? 94 : 86;
    const int input_spacing = portrait ? 38 : 32;
    const int input_height = portrait ? 36 : 28;
    const int keyboard_y = portrait ? 320 : 222;
    ip_mode_dropdown_ = lv_dropdown_create(lv_screen_active());
    lv_dropdown_set_options(ip_mode_dropdown_,
                            "Mode: DHCP-Client\nMode: Static-address\nMode: DHCP-server");
    lv_dropdown_set_selected(ip_mode_dropdown_, 0U);
    lv_obj_set_size(ip_mode_dropdown_, screen_width() - 2 * kHorizontalMargin, input_height);
    lv_obj_align(ip_mode_dropdown_, LV_ALIGN_TOP_MID, 0, mode_y);
    lv_obj_add_event_cb(ip_mode_dropdown_, ip_mode_callback, LV_EVENT_VALUE_CHANGED, this);
    // LVGL's popup list is a separate object layered over the screen. Give
    // both it and the closed selector an opaque, high-contrast frame so the
    // choices neither blend into the page nor look like floating text.
    const UiThemeSkin& skin = theme_.active_skin();
    lv_obj_set_style_bg_color(ip_mode_dropdown_, UiTheme::to_lv_color(skin.colors.surface), 0);
    lv_obj_set_style_bg_opa(ip_mode_dropdown_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ip_mode_dropdown_, UiTheme::to_lv_color(skin.colors.text), 0);
    lv_obj_set_style_border_width(ip_mode_dropdown_, std::max(1, skin.shape.border_width), 0);
    lv_obj_set_style_border_color(ip_mode_dropdown_, UiTheme::to_lv_color(skin.colors.accent), 0);
    lv_obj_set_style_radius(ip_mode_dropdown_, skin.shape.radius, 0);
    lv_obj_t* const mode_list = lv_dropdown_get_list(ip_mode_dropdown_);
    if (mode_list != nullptr) {
        lv_obj_set_style_bg_color(mode_list, UiTheme::to_lv_color(skin.colors.surface), 0);
        lv_obj_set_style_bg_opa(mode_list, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(mode_list, UiTheme::to_lv_color(skin.colors.text), 0);
        lv_obj_set_style_text_font(mode_list, skin.fonts.body, 0);
        lv_obj_set_style_border_width(mode_list, std::max(2, skin.shape.border_width + 1), 0);
        lv_obj_set_style_border_color(mode_list, UiTheme::to_lv_color(skin.colors.accent), 0);
        lv_obj_set_style_radius(mode_list, skin.shape.radius, 0);
        // The three generously-sized rows make each mode practical on a small
        // touch panel. Dividers are drawn in the list's draw callback rather
        // than represented by accidentally selectable dropdown options.
        lv_obj_set_style_pad_ver(mode_list, 8, 0);
        lv_obj_set_style_pad_hor(mode_list, 8, 0);
        lv_obj_set_style_text_line_space(mode_list, 22, LV_PART_MAIN);
        lv_obj_set_style_text_line_space(mode_list, 22, LV_PART_SELECTED);
        lv_obj_set_style_bg_color(mode_list, UiTheme::to_lv_color(skin.colors.chrome),
                                  LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(mode_list, LV_OPA_COVER,
                                LV_PART_SELECTED | LV_STATE_CHECKED);
        // The custom row dividers below are the sole separators. A border on
        // LVGL's selected item sits a few pixels above the first divider and
        // produced a visible double line on the physical panel.
        lv_obj_set_style_border_width(mode_list, 0,
                                      LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_add_event_cb(mode_list, ip_mode_list_draw_callback, LV_EVENT_DRAW_MAIN, this);
    }

    // 96 px keeps the two lease labels on one line. The remaining 186 px is
    // still ample for an IPv4 address, and lets the four rows use a compact
    // 2 px vertical gap instead of making the labels overlap the next field.
    const int label_width = portrait ? 96 : 68;
    const int label_gap = 6;
    const int field_x = kHorizontalMargin + label_width + label_gap;
    const int field_width = screen_width() - kHorizontalMargin - field_x;
    const int label_y_offset = portrait ? 9 : 6;
    const auto create_labeled_ip_input =
        [this, field_x, field_width, input_height, label_width, label_y_offset](
            const char* label_text, int y, lv_obj_t** label, lv_obj_t** input) {
            *label = lv_label_create(lv_screen_active());
            lv_label_set_text(*label, label_text);
            lv_obj_set_width(*label, label_width);
            lv_obj_set_pos(*label, kHorizontalMargin, y + label_y_offset);
            UiTheme::set_role(*label, UiThemeRole::DimText);
            create_ip_input(label_text, y, input_height, "0123456789.", input);
            // create_ip_input centres ordinary full-width fields. This inline
            // form instead uses screen-relative coordinates, so clear that
            // alignment before supplying its left edge.
            lv_obj_set_align(*input, LV_ALIGN_DEFAULT);
            lv_obj_set_size(*input, field_width, input_height);
            lv_obj_set_pos(*input, field_x, y);
        };
    create_labeled_ip_input("IP address", input_y, &ip_address_label_, &ip_address_input_);
    create_labeled_ip_input("Gateway", input_y + input_spacing, &gateway_label_, &gateway_input_);
    create_labeled_ip_input("Netmask", input_y + 2 * input_spacing, &netmask_label_, &netmask_input_);
    create_labeled_ip_input("Lease end", input_y + 3 * input_spacing,
                            &lease_end_label_, &lease_end_input_);

    if (portrait) {
        ip_apply_button_ = create_button(applying_enabled ? "Apply settings" : "Validate inputs",
                                          214, "__validate_ip");
        ip_back_button_ = create_button("Back", 266, "__back");
    } else {
        const int gap = 8;
        const int width = (screen_width() - 2 * kHorizontalMargin - gap) / 2;
        ip_apply_button_ = create_button(applying_enabled ? "Apply" : "Validate",
                                         kHorizontalMargin, 184, width, 34, "__validate_ip");
        ip_back_button_ = create_button("Back", kHorizontalMargin + width + gap, 184, width, 34,
                                        "__back");
    }

    keyboard_ = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(keyboard_, ip_address_input_);
    lv_obj_set_size(keyboard_, screen_width(), screen_height() - keyboard_y);
    lv_obj_align(keyboard_, LV_ALIGN_TOP_MID, 0, keyboard_y);
    lv_obj_add_event_cb(keyboard_, keyboard_callback, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_callback, LV_EVENT_CANCEL, this);
    update_ip_settings_mode();
    if (request_managed_ipv4_profile_) {
        request_managed_ipv4_profile_();
    }
}

void StarterUi::load_managed_ipv4_profile(const core::ManagedIpv4Profile& profile) {
    if (!ip_settings_visible_ || ip_settings_profile_loaded_ ||
        profile.interface_name != static_ip_interface_ || ip_mode_dropdown_ == nullptr ||
        ip_address_input_ == nullptr || gateway_input_ == nullptr || netmask_input_ == nullptr) {
        return;
    }
    if (profile.method == "auto") {
        lv_dropdown_set_selected(ip_mode_dropdown_, 0U);
        ip_settings_profile_loaded_ = true;
        update_ip_settings_mode();
        return;
    }
    if (profile.method != "manual") {
        return;
    }
    const std::size_t slash = profile.address_with_prefix.find('/');
    if (slash == std::string::npos || slash == 0U ||
        slash + 1U >= profile.address_with_prefix.size()) {
        std::cerr << "Ignoring saved manual IPv4 profile for " << static_ip_interface_
                  << ": expected exactly one address/prefix.\n";
        ip_settings_profile_loaded_ = true;
        return;
    }
    const auto netmask = dotted_netmask_from_prefix(
        std::string_view(profile.address_with_prefix).substr(slash + 1U));
    if (!netmask.has_value()) {
        std::cerr << "Ignoring saved manual IPv4 profile for " << static_ip_interface_
                  << ": expected exactly one address/prefix.\n";
        ip_settings_profile_loaded_ = true;
        return;
    }
    lv_textarea_set_text(ip_address_input_, profile.address_with_prefix.substr(0U, slash).c_str());
    lv_textarea_set_text(netmask_input_, netmask->c_str());
    if (profile.dhcp_server_active && lease_end_input_ != nullptr) {
        lv_textarea_set_text(gateway_input_, profile.dhcp_server_lease_start.c_str());
        lv_textarea_set_text(lease_end_input_, profile.dhcp_server_lease_end.c_str());
        lv_dropdown_set_selected(ip_mode_dropdown_, 2U);
    } else {
        lv_textarea_set_text(gateway_input_, profile.gateway.c_str());
        lv_dropdown_set_selected(ip_mode_dropdown_, 1U);
    }
    ip_settings_profile_loaded_ = true;
    update_ip_settings_mode();
}

void StarterUi::set_static_ipv4_defaults() {
    if (ip_address_input_ == nullptr || gateway_input_ == nullptr || netmask_input_ == nullptr) {
        return;
    }
    // This runs only after an explicit selector change. Saved profiles are
    // rendered directly instead, preserving even an intentionally empty
    // gateway. Resetting here prevents DHCP-server lease values from becoming
    // an accidental static address/gateway pair when a user changes modes.
    lv_textarea_set_text(ip_address_input_, "192.168.1.1");
    lv_textarea_set_text(gateway_input_, "192.168.1.1");
    lv_textarea_set_text(netmask_input_, "255.255.255.0");
}

void StarterUi::set_dhcp_server_defaults() {
    if (ip_address_input_ == nullptr || gateway_input_ == nullptr || netmask_input_ == nullptr ||
        lease_end_input_ == nullptr) {
        return;
    }
    // Like Static-Address, an explicit mode switch starts with a complete,
    // internally consistent isolated subnet rather than borrowing fields
    // from the preceding DHCP-client or static form.
    lv_textarea_set_text(ip_address_input_, "192.168.50.1");
    lv_textarea_set_text(gateway_input_, "192.168.50.100");
    lv_textarea_set_text(netmask_input_, "255.255.255.0");
    lv_textarea_set_text(lease_end_input_, "192.168.50.200");
}

void StarterUi::show_network_result(std::string message, bool ok, bool pending) {
    clear_screen();
    screen_id_ = "network_result";
    network_result_visible_ = true;
    create_title("IP Settings");

    network_result_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(network_result_label_, screen_width() - 2 * kHorizontalMargin);
    lv_label_set_long_mode(network_result_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(network_result_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(network_result_label_, message.c_str());
    lv_obj_align(network_result_label_, LV_ALIGN_CENTER, 0, -24);
    UiTheme::set_role(network_result_label_, pending ? UiThemeRole::DimText
                                                      : (ok ? UiThemeRole::SuccessText
                                                            : UiThemeRole::ErrorText));
    if (!pending) {
        create_button("Back", screen_height() - button_height() - 12, "__back");
    }
}

void StarterUi::show_system_update() {
    clear_screen();
    screen_id_ = "software_update";
    system_update_visible_ = true;
    create_title("Software Update");

    system_update_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(system_update_label_, screen_width() - 2 * kHorizontalMargin);
    lv_label_set_long_mode(system_update_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(system_update_label_, LV_TEXT_ALIGN_CENTER, 0);
    const std::string status = system_update_status_
        ? system_update_status_()
        : "A/B update status is unavailable.";
    const std::string message = status +
        "\n\nCopy the " + std::string{core::kSystemUpdateBundleExtension} +
        " update file onto any USB stick, plug it in, then choose Check USB stick."
        " The inactive slot is verified before candidate boot."
        "\n\nRecovery: if the candidate does not return, remove and reapply power."
        " The one-shot candidate is abandoned and the committed slot boots again.";
    lv_label_set_text(system_update_label_, message.c_str());
    lv_obj_align(system_update_label_, LV_ALIGN_TOP_MID, 0, 52);
    UiTheme::set_role(system_update_label_, UiThemeRole::DimText);

    const int check_y = screen_height() - 2 * button_height() - 20;
    create_button("Check USB stick", check_y, "__check_system_update");
    create_button("Back", screen_height() - button_height() - 12, "__back");
}

void StarterUi::show_system_update_result(std::string message, bool ok, bool pending) {
    clear_screen();
    screen_id_ = "software_update_result";
    system_update_result_visible_ = true;
    create_title("Software Update");

    system_update_result_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(system_update_result_label_, screen_width() - 2 * kHorizontalMargin);
    lv_label_set_long_mode(system_update_result_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(system_update_result_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(system_update_result_label_, message.c_str());
    lv_obj_align(system_update_result_label_, LV_ALIGN_CENTER, 0, -24);
    UiTheme::set_role(system_update_result_label_, pending ? UiThemeRole::DimText
                                                           : (ok ? UiThemeRole::SuccessText
                                                                 : UiThemeRole::ErrorText));
    if (!pending) {
        create_button("Back", screen_height() - button_height() - 12, "__back");
    }
}

void StarterUi::update_ip_settings_mode() {
    if (!ip_settings_visible_ || ip_mode_dropdown_ == nullptr || ip_address_label_ == nullptr ||
        ip_address_input_ == nullptr || gateway_label_ == nullptr || gateway_input_ == nullptr ||
        netmask_label_ == nullptr || netmask_input_ == nullptr || lease_end_label_ == nullptr ||
        lease_end_input_ == nullptr || ip_status_label_ == nullptr || ip_apply_button_ == nullptr ||
        ip_back_button_ == nullptr || keyboard_ == nullptr) {
        return;
    }
    const std::uint16_t mode = lv_dropdown_get_selected(ip_mode_dropdown_);
    const bool static_mode = mode == 1U;
    const bool dhcp_server_mode = mode == 2U;
    const bool portrait = screen_height() > screen_width();
    const int static_button_width = portrait ? screen_width() - 2 * kHorizontalMargin
                                             : (screen_width() - 2 * kHorizontalMargin - 8) / 2;
    const int static_button_height = portrait ? button_height() : 34;
    const int static_apply_x = kHorizontalMargin;
    const int static_back_x = portrait ? kHorizontalMargin
                                       : kHorizontalMargin + static_button_width + 8;
    // Static-address has three fields. Keep its 48 px action targets, but
    // close the former 22 px dead space after Netmask and return 14 px to the
    // numeric keyboard.
    const int static_apply_y = portrait ? 214 : 184;
    const int static_back_y = portrait ? 266 : 184;
    const int full_button_width = screen_width() - 2 * kHorizontalMargin;
    const int dhcp_apply_y = screen_height() - 2 * button_height() - 20;
    const int dhcp_back_y = screen_height() - button_height() - 12;
    const int server_button_width = portrait ? full_button_width : static_button_width;
    // The portrait server form has four inline fields. Four-pixel gaps still
    // keep the 36 px touch targets visually distinct while returning 12 px
    // to the numeric keyboard on the 320x480 panel.
    const int server_button_height = portrait ? 36 : 28;
    const int server_apply_x = kHorizontalMargin;
    const int server_back_x = portrait ? kHorizontalMargin
                                       : kHorizontalMargin + server_button_width + 8;
    const int server_apply_y = portrait ? 248 : 216;
    const int server_back_y = portrait ? 288 : 216;
    const int server_keyboard_y = portrait ? 328 : 250;

    const std::string normal_status = request_network_change_
        ? "Choose a network mode for " + static_ip_interface_ + "."
        : "Choose a network mode; no network changes.";
    lv_label_set_text(ip_status_label_, dhcp_server_mode
        ? "Isolated eth0: no router or DNS."
        : normal_status.c_str());
    UiTheme::set_role(ip_status_label_, UiThemeRole::DimText);
    if (static_mode) {
        lv_label_set_text(ip_address_label_, "IP address");
        lv_label_set_text(gateway_label_, "Gateway");
        lv_label_set_text(netmask_label_, "Netmask");
        lv_obj_remove_flag(ip_address_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ip_address_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(gateway_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(gateway_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(netmask_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(netmask_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lease_end_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lease_end_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(ip_apply_button_, static_button_width, static_button_height);
        lv_obj_set_size(ip_back_button_, static_button_width, static_button_height);
        lv_obj_set_pos(ip_apply_button_, static_apply_x, static_apply_y);
        lv_obj_set_pos(ip_back_button_, static_back_x, static_back_y);
        lv_obj_set_size(keyboard_, screen_width(), screen_height() - (portrait ? 320 : 222));
        lv_obj_set_pos(keyboard_, 0, portrait ? 320 : 222);
        if (lv_obj_get_child_count(ip_apply_button_) != 0U) {
            lv_label_set_text(lv_obj_get_child(ip_apply_button_, 0U),
                              request_network_change_ ? "Apply settings" : "Validate inputs");
        }
        focus_ip_input(ip_address_input_);
    } else if (dhcp_server_mode) {
        lv_label_set_text(ip_address_label_, "Server IP");
        lv_label_set_text(gateway_label_, "Lease start");
        lv_label_set_text(netmask_label_, "Netmask");
        lv_label_set_text(lease_end_label_, "Lease end");
        lv_obj_remove_flag(ip_address_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ip_address_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(gateway_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(gateway_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(netmask_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(netmask_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lease_end_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lease_end_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(ip_apply_button_, server_button_width, server_button_height);
        lv_obj_set_size(ip_back_button_, server_button_width, server_button_height);
        lv_obj_set_pos(ip_apply_button_, server_apply_x, server_apply_y);
        lv_obj_set_pos(ip_back_button_, server_back_x, server_back_y);
        lv_obj_set_size(keyboard_, screen_width(), screen_height() - server_keyboard_y);
        lv_obj_set_pos(keyboard_, 0, server_keyboard_y);
        if (lv_obj_get_child_count(ip_apply_button_) != 0U) {
            lv_label_set_text(lv_obj_get_child(ip_apply_button_, 0U),
                              dhcp_server_apply_confirmed_ ? "Confirm enable"
                                                           : "Enable DHCP server");
        }
        focus_ip_input(ip_address_input_);
    } else {
        lv_obj_add_flag(ip_address_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ip_address_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gateway_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gateway_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(netmask_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(netmask_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lease_end_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lease_end_input_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(ip_apply_button_, full_button_width, button_height());
        lv_obj_set_size(ip_back_button_, full_button_width, button_height());
        lv_obj_set_x(ip_apply_button_, kHorizontalMargin);
        lv_obj_set_x(ip_back_button_, kHorizontalMargin);
        dismiss_keyboard();
        lv_obj_set_y(ip_apply_button_, dhcp_apply_y);
        lv_obj_set_y(ip_back_button_, dhcp_back_y);
        if (lv_obj_get_child_count(ip_apply_button_) != 0U) {
            lv_label_set_text(lv_obj_get_child(ip_apply_button_, 0U),
                              request_network_change_ ? "Apply settings" : "Validate inputs");
        }
    }
}

void StarterUi::show_wifi() {
    clear_screen();
    screen_id_ = "wifi";
    wifi_scan_visible_ = true;
    create_title("Wi-Fi Networks");

    wifi_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(wifi_label_, screen_width() - 2 * kHorizontalMargin);
    lv_label_set_long_mode(wifi_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(wifi_label_, LV_ALIGN_TOP_MID, 0, 52);
    UiTheme::set_role(wifi_label_, UiThemeRole::DimText);

    create_button("Scan again", screen_height() - 2 * button_height() - 20, "__wifi_scan");
    create_button("Back", screen_height() - button_height() - 12, "__back");
    request_wifi_scan();
}

void StarterUi::show_wifi_password_demo() {
    clear_screen();
    screen_id_ = "wifi_password_demo";
    wifi_password_visible_ = true;
    create_title("Wi-Fi Password");

    const bool portrait = screen_height() > screen_width();
    const int input_y = portrait ? 72 : 62;
    const int input_height = portrait ? 44 : 36;
    const int status_y = input_y + input_height + 6;
    const int keyboard_y = portrait ? 192 : 150;

    lv_obj_t* const note = lv_label_create(lv_screen_active());
    // Keep this ASCII-only: the compact panel font intentionally omits the
    // em dash, which otherwise renders as a missing-glyph square.
    lv_label_set_text(note, "Mock join only - no network changes");
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, portrait ? 46 : 42);
    UiTheme::set_role(note, UiThemeRole::DimText);

    wifi_password_input_ = lv_textarea_create(lv_screen_active());
    lv_textarea_set_one_line(wifi_password_input_, true);
    lv_textarea_set_placeholder_text(wifi_password_input_, "Wi-Fi password");
    lv_textarea_set_password_mode(wifi_password_input_, true);
    // Do not briefly expose a newly entered character in the default state.
    // The eye control below is the user's explicit temporary reveal action.
    lv_textarea_set_password_show_time(wifi_password_input_, 0);
    lv_textarea_set_max_length(wifi_password_input_, 63);
    lv_obj_set_size(wifi_password_input_, screen_width() - 2 * kHorizontalMargin,
                    input_height);
    lv_obj_align(wifi_password_input_, LV_ALIGN_TOP_MID, 0, input_y);
    // Reserve the right edge for the eye control so typed text and cursor do
    // not run below its touch target.
    lv_obj_set_style_pad_right(wifi_password_input_, 54, 0);
    lv_textarea_set_cursor_click_pos(wifi_password_input_, true);
    lv_obj_add_event_cb(wifi_password_input_, wifi_password_input_callback,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(wifi_password_input_, wifi_password_input_callback,
                        LV_EVENT_CLICKED, this);

    wifi_password_visibility_control_ = std::make_unique<PasswordVisibilityControl>();
    configure_password_visibility_control(wifi_password_visibility_control_.get(),
                                          wifi_password_input_, input_y, input_height - 4);

    wifi_password_length_label_ = lv_label_create(lv_screen_active());
    lv_obj_align(wifi_password_length_label_, LV_ALIGN_TOP_MID, 0, status_y);
    UiTheme::set_role(wifi_password_length_label_, UiThemeRole::DimText);

    wifi_password_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(wifi_password_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(wifi_password_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(wifi_password_status_label_, LV_ALIGN_TOP_MID, 0, status_y);
    lv_obj_add_flag(wifi_password_status_label_, LV_OBJ_FLAG_HIDDEN);

    if (portrait) {
        create_button("Back", kHorizontalMargin, 150, screen_width() - 2 * kHorizontalMargin,
                      36, "__back");
    } else {
        create_button("Back", screen_width() - kHorizontalMargin - 104, 108, 104, 34, "__back");
    }

    keyboard_ = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_map(keyboard_, LV_KEYBOARD_MODE_USER_1, kPasswordKeyboardLowerPageOne,
                        kPasswordKeyboardLowerPageOneControls);
    lv_keyboard_set_map(keyboard_, LV_KEYBOARD_MODE_USER_2, kPasswordKeyboardLowerPageTwo,
                        kPasswordKeyboardLowerPageTwoControls);
    lv_keyboard_set_map(keyboard_, LV_KEYBOARD_MODE_USER_3, kPasswordKeyboardUpperPageOne,
                        kPasswordKeyboardUpperPageOneControls);
    lv_keyboard_set_map(keyboard_, LV_KEYBOARD_MODE_USER_4, kPasswordKeyboardUpperPageTwo,
                        kPasswordKeyboardUpperPageTwoControls);
    lv_keyboard_set_map(keyboard_, LV_KEYBOARD_MODE_SPECIAL, kPasswordKeyboardSymbols,
                        kPasswordKeyboardSymbolsControls);
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_textarea(keyboard_, wifi_password_input_);
    lv_keyboard_set_popovers(keyboard_, true);
    lv_obj_set_size(keyboard_, screen_width(), screen_height() - keyboard_y);
    lv_obj_align(keyboard_, LV_ALIGN_TOP_MID, 0, keyboard_y);
    lv_obj_add_event_cb(keyboard_, wifi_password_keyboard_navigation_callback,
                        static_cast<lv_event_code_t>(LV_EVENT_PREPROCESS | LV_EVENT_VALUE_CHANGED),
                        this);
    lv_obj_add_event_cb(keyboard_, wifi_password_keyboard_callback, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, wifi_password_keyboard_callback, LV_EVENT_CANCEL, this);
    lv_obj_add_state(wifi_password_input_, LV_STATE_FOCUSED);
    lv_obj_send_event(wifi_password_input_, LV_EVENT_FOCUSED, nullptr);
    update_wifi_password_length();
}

void StarterUi::show_theme_selection() {
    clear_screen();
    screen_id_ = "theme_select";
    create_title("Theme");

    lv_obj_t* const status = lv_label_create(lv_screen_active());
    const std::string active_name = active_theme_name_ ? active_theme_name_() : "unknown";
    const std::string text = theme_message_.empty() ? "Active: " + active_name : theme_message_;
    lv_label_set_text(status, text.c_str());
    UiTheme::set_role(status, theme_message_.empty() ? UiThemeRole::DimText : UiThemeRole::ErrorText);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 44);

    StarterMenuPresentation presentation;
    create_menu_content(presentation, 76);
    create_menu_button("Dark", "", "", "__theme:dark", presentation);
    create_menu_button("Light", "", "", "__theme:light", presentation);
    create_menu_button("High contrast", "", "", "__theme:high-contrast", presentation);
    create_menu_button("Back", "back", "", "__back", presentation);
}

void StarterUi::show_progress_demo() {
    clear_screen();
    screen_id_ = "progress_demo";
    create_title("Progress Demo");

    progress_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(progress_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(progress_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(progress_label_, LV_ALIGN_TOP_MID, 0, 92);

    progress_bar_ = lv_bar_create(lv_screen_active());
    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_size(progress_bar_, screen_width() - 2 * kHorizontalMargin, 28);
    lv_obj_align(progress_bar_, LV_ALIGN_TOP_MID, 0, 136);

    create_button("Back", screen_height() - button_height() - 12, "__back");
    progress_started_at_ = std::chrono::steady_clock::now();
    progress_timer_ = lv_timer_create(progress_timer_callback, kProgressDemoPeriodMs, this);
    update_progress_demo();
}

void StarterUi::show_action_runner_demo() {
    clear_screen();
    screen_id_ = "action_runner_demo";
    action_runner_visible_ = true;
    action_runner_running_ = true;
    action_runner_job_id_ = next_action_runner_job_id_++;
    create_title("Action Runner Demo");

    action_runner_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(action_runner_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(action_runner_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    action_runner_status_text_ = "Starting simulated flash...";
    lv_label_set_text(action_runner_status_label_, action_runner_status_text_.c_str());
    UiTheme::set_role(action_runner_status_label_, UiThemeRole::DimText);
    lv_obj_align(action_runner_status_label_, LV_ALIGN_TOP_MID, 0, 52);

    action_runner_bar_ = lv_bar_create(lv_screen_active());
    lv_bar_set_range(action_runner_bar_, 0, 100);
    lv_bar_set_value(action_runner_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_size(action_runner_bar_, screen_width() - 2 * kHorizontalMargin, 24);
    lv_obj_align(action_runner_bar_, LV_ALIGN_TOP_MID, 0, 88);

    action_runner_log_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(action_runner_log_label_, screen_width() - 2 * kHorizontalMargin);
    lv_label_set_long_mode(action_runner_log_label_, LV_LABEL_LONG_WRAP);
    action_runner_log_text_ = "Waiting for action output...";
    lv_label_set_text(action_runner_log_label_, action_runner_log_text_.c_str());
    UiTheme::set_role(action_runner_log_label_, UiThemeRole::DimText);
    lv_obj_align(action_runner_log_label_, LV_ALIGN_TOP_MID, 0, 126);

    action_runner_cancel_button_ =
        create_button("Cancel", screen_height() - 2 * button_height() - 20, "__cancel_action");
    create_button("Back", screen_height() - button_height() - 12, "__back");
    action_progress_timer_ =
        lv_timer_create(action_progress_timer_callback, kActionProgressPeriodMs, this);
    if (start_action_demo_ == nullptr || !start_action_demo_(action_runner_job_id_)) {
        action_runner_running_ = false;
        if (action_runner_cancel_button_ != nullptr) {
            lv_obj_add_flag(action_runner_cancel_button_, LV_OBJ_FLAG_HIDDEN);
        }
        if (action_runner_status_label_ != nullptr) {
            action_runner_status_text_ = "Unable to start action";
            lv_label_set_text(action_runner_status_label_, action_runner_status_text_.c_str());
            UiTheme::set_role(action_runner_status_label_, UiThemeRole::ErrorText);
        }
    }
}

void StarterUi::show_slider_demo() {
    clear_screen();
    screen_id_ = "slider_demo";
    create_title("Slider Demo");

    const bool portrait = screen_height() > screen_width();
    brightness_slider_label_ = lv_label_create(lv_screen_active());
    lv_obj_align(brightness_slider_label_, LV_ALIGN_TOP_MID, 0, 58);

    brightness_slider_ = lv_slider_create(lv_screen_active());
    lv_slider_set_range(brightness_slider_, 0, 100);
    lv_slider_set_value(brightness_slider_, 60, LV_ANIM_OFF);
    lv_slider_set_orientation(brightness_slider_, LV_SLIDER_ORIENTATION_HORIZONTAL);
    lv_obj_set_size(brightness_slider_, screen_width() - 2 * kHorizontalMargin -
                                            2 * kSliderHitPadding,
                    kSliderTrackThickness);
    lv_obj_align(brightness_slider_, LV_ALIGN_TOP_MID, 0, 100);
    configure_demo_slider_interaction(brightness_slider_);
    lv_obj_add_event_cb(brightness_slider_, slider_callback, LV_EVENT_VALUE_CHANGED, this);

    volume_slider_label_ = lv_label_create(lv_screen_active());
    lv_obj_align(volume_slider_label_, LV_ALIGN_TOP_MID, 0, portrait ? 156 : 138);

    volume_slider_ = lv_slider_create(lv_screen_active());
    lv_slider_set_range(volume_slider_, 0, 100);
    lv_slider_set_value(volume_slider_, 45, LV_ANIM_OFF);
    lv_slider_set_orientation(volume_slider_, LV_SLIDER_ORIENTATION_VERTICAL);
    lv_obj_set_size(volume_slider_, kSliderTrackThickness, portrait ? 170 : 88);
    lv_obj_align(volume_slider_, LV_ALIGN_TOP_MID, 0, portrait ? 200 : 160);
    configure_demo_slider_interaction(volume_slider_);
    lv_obj_add_event_cb(volume_slider_, slider_callback, LV_EVENT_VALUE_CHANGED, this);

    create_button("Back", screen_height() - button_height() - 12, "__back");
    update_slider_demo();
}

void StarterUi::show_display_brightness() {
    clear_screen();
    screen_id_ = "brightness";
    create_title("Brightness", 8);

    if (display_brightness_settings_provider_) {
        const auto settings = display_brightness_settings_provider_();
        if (settings.has_value()) {
            display_brightness_settings_ = *settings;
            applied_display_brightness_settings_ = *settings;
            display_brightness_available_ = static_cast<bool>(preview_display_brightness_) &&
                                            static_cast<bool>(apply_display_brightness_settings_);
        }
    }

    display_brightness_label_ = lv_label_create(lv_screen_active());
    lv_obj_align(display_brightness_label_, LV_ALIGN_TOP_MID, 0, 66);

    display_brightness_slider_ = lv_slider_create(lv_screen_active());
    lv_slider_set_range(display_brightness_slider_,
                        static_cast<int>(platform::kDisplayBrightnessMinimumPercent),
                        static_cast<int>(platform::kDisplayBrightnessMaximumPercent));
    lv_obj_set_size(display_brightness_slider_, screen_width() - 2 * kHorizontalMargin -
                                                  2 * kSliderHitPadding,
                    kSliderTrackThickness);
    lv_obj_align(display_brightness_slider_, LV_ALIGN_TOP_MID, 0, 108);
    configure_demo_slider_interaction(display_brightness_slider_);
    lv_obj_add_event_cb(display_brightness_slider_, display_brightness_slider_callback,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(display_brightness_slider_, display_brightness_slider_callback,
                        LV_EVENT_RELEASED, this);

    display_brightness_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(display_brightness_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(display_brightness_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(display_brightness_status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(display_brightness_status_label_, LV_ALIGN_TOP_MID, 0, 160);

    create_button("Back", screen_height() - button_height() - 12, "__back");
    update_display_brightness_controls();
    if (!display_brightness_available_ && display_brightness_status_label_ != nullptr) {
        display_brightness_status_text_ = "Brightness control is unavailable for this panel";
        lv_label_set_text(display_brightness_status_label_, display_brightness_status_text_.c_str());
        UiTheme::set_role(display_brightness_status_label_, UiThemeRole::DimText);
    }
}

void StarterUi::show_display_standby() {
    clear_screen();
    screen_id_ = "display_standby";
    create_title("Standby", 8);

    if (display_standby_settings_provider_) {
        const auto settings = display_standby_settings_provider_();
        if (settings.has_value()) {
            display_standby_settings_ = *settings;
            applied_display_standby_settings_ = *settings;
            display_standby_available_ = static_cast<bool>(apply_display_standby_settings_);
        }
    }

    display_standby_checkbox_ = lv_checkbox_create(lv_screen_active());
    lv_checkbox_set_text(display_standby_checkbox_, "Enable auto standby");
    lv_obj_set_size(display_standby_checkbox_, screen_width() - 2 * kHorizontalMargin, 48);
    lv_obj_align(display_standby_checkbox_, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_add_event_cb(display_standby_checkbox_, display_standby_checkbox_callback,
                        LV_EVENT_VALUE_CHANGED, this);

    display_standby_label_ = lv_label_create(lv_screen_active());
    lv_obj_align(display_standby_label_, LV_ALIGN_TOP_MID, 0, 124);

    display_standby_slider_ = lv_slider_create(lv_screen_active());
    lv_slider_set_range(display_standby_slider_,
                        static_cast<int>(platform::kDisplayStandbyMinimumSeconds),
                        static_cast<int>(platform::kDisplayStandbyMaximumSeconds));
    lv_obj_set_size(display_standby_slider_, screen_width() - 2 * kHorizontalMargin -
                                                2 * kSliderHitPadding,
                    kSliderTrackThickness);
    lv_obj_align(display_standby_slider_, LV_ALIGN_TOP_MID, 0, 166);
    configure_demo_slider_interaction(display_standby_slider_);
    lv_obj_add_event_cb(display_standby_slider_, display_standby_slider_callback,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(display_standby_slider_, display_standby_slider_callback,
                        LV_EVENT_RELEASED, this);

    display_standby_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(display_standby_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(display_standby_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(display_standby_status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(display_standby_status_label_, LV_ALIGN_TOP_MID, 0, 218);

    const int back_y = screen_height() - button_height() - 12;
    if (screen_height() > screen_width()) {
        display_standby_apply_button_ =
            create_button("Apply", back_y - button_height() - 8, "__apply_display_standby");
        create_button("Back", back_y, "__back");
    } else {
        const int gap = 8;
        const int width = (screen_width() - 2 * kHorizontalMargin - gap) / 2;
        display_standby_apply_button_ = create_button(
            "Apply", kHorizontalMargin, back_y, width, button_height(), "__apply_display_standby");
        create_button("Back", kHorizontalMargin + width + gap, back_y, width, button_height(),
                      "__back");
    }
    update_display_standby_controls();
    if (!display_standby_available_ && display_standby_status_label_ != nullptr) {
        display_standby_status_text_ = "Auto standby is unavailable for this panel";
        lv_label_set_text(display_standby_status_label_, display_standby_status_text_.c_str());
        UiTheme::set_role(display_standby_status_label_, UiThemeRole::DimText);
    }
}

void StarterUi::show_screen_lock_settings() {
    clear_screen();
    screen_id_ = "screen_lock_settings";
    screen_lock_settings_visible_ = true;
    create_title("Screen Lock", 8);

    screen_lock_available_ = static_cast<bool>(screen_lock_settings_provider_) &&
                             static_cast<bool>(set_screen_lock_pin_) &&
                             static_cast<bool>(set_screen_lock_enabled_) &&
                             static_cast<bool>(verify_screen_lock_pin_) &&
                             static_cast<bool>(set_screen_lock_session_);
    std::optional<platform::ScreenLockSettings> settings;
    if (screen_lock_available_) {
        settings = screen_lock_settings_provider_();
    }
    if (!settings.has_value()) {
        screen_lock_available_ = false;
    }

    lv_obj_t* const guidance = lv_label_create(lv_screen_active());
    lv_obj_set_width(guidance, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(guidance, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(guidance, LV_LABEL_LONG_WRAP);
    lv_label_set_text(guidance, "Choose a PIN between 4 and 10 digits.");
    lv_obj_align(guidance, LV_ALIGN_TOP_MID, 0, 48);
    UiTheme::set_role(guidance, UiThemeRole::DimText);

    screen_lock_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(screen_lock_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(screen_lock_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(screen_lock_status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(screen_lock_status_label_, LV_ALIGN_TOP_MID, 0, 82);

    if (!screen_lock_available_) {
        screen_lock_status_text_ = "Screen lock is unavailable: persistent storage is required.";
        lv_label_set_text(screen_lock_status_label_, screen_lock_status_text_.c_str());
        UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        create_button("Back", screen_height() - button_height() - 12, "__back");
        return;
    }

    screen_lock_status_text_ = settings->enabled
        ? "Screen lock is enabled. It locks after standby wakes."
        : "Screen lock is disabled.";
    lv_label_set_text(screen_lock_status_label_, screen_lock_status_text_.c_str());
    UiTheme::set_role(screen_lock_status_label_, UiThemeRole::DimText);

    const int first_button_y = 132;
    create_button(settings->configured ? "Change PIN" : "Set PIN", first_button_y,
                  "__screen_lock_set_pin");
    if (settings->enabled) {
        create_button("Disable screen lock", first_button_y + 60, "__screen_lock_disable");
        create_button("Lock now", first_button_y + 120, "__screen_lock_now");
    } else {
        create_button("Enable screen lock", first_button_y + 60, "__screen_lock_enable");
    }
    create_button("Back", screen_height() - button_height() - 12, "__back");
}

void StarterUi::configure_screen_lock_input(lv_obj_t* input, const char* placeholder, int y) {
    if (input == nullptr) {
        return;
    }
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_placeholder_text(input, placeholder);
    lv_textarea_set_accepted_chars(input, "0123456789");
    lv_textarea_set_max_length(input, platform::kScreenLockPinMaximumDigits);
    lv_textarea_set_password_mode(input, true);
    // Never briefly reveal the last entered digit on this security screen.
    lv_textarea_set_password_show_time(input, 0U);
    lv_textarea_set_cursor_click_pos(input, true);
    lv_obj_remove_flag(input, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_size(input, screen_width() - 2 * kHorizontalMargin, 40);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(input, screen_lock_input_callback, LV_EVENT_CLICKED, this);
}

void StarterUi::configure_password_visibility_control(PasswordVisibilityControl* control,
                                                       lv_obj_t* input, int y, int height) {
    if (control == nullptr || input == nullptr || height <= 0) {
        return;
    }
    control->input = input;
    // Keep the eye inside the existing edit field without reducing its hit
    // area. The extra padding reserves room for both the icon and cursor.
    lv_obj_set_style_pad_right(input, 54, 0);
    control->button = lv_button_create(lv_screen_active());
    lv_obj_set_size(control->button, 44, height);
    lv_obj_set_pos(control->button, screen_width() - kHorizontalMargin - 46, y + 2);
    lv_obj_add_flag(control->button, LV_OBJ_FLAG_CHECKABLE);
    // It looks like an icon inside the edit field while retaining a 40+ px
    // touch target and clear pressed feedback on portrait forms.
    lv_obj_set_style_bg_opa(control->button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(control->button, 0, 0);
    lv_obj_set_style_bg_color(control->button,
                              UiTheme::to_lv_color(theme_.active_skin().colors.chrome),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(control->button, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_add_event_cb(control->button, password_visibility_callback, LV_EVENT_VALUE_CHANGED,
                        control);
    control->icon = lv_label_create(control->button);
    lv_label_set_text(control->icon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(control->icon,
                                UiTheme::to_lv_color(theme_.active_skin().colors.accent), 0);
    lv_obj_center(control->icon);
}

void StarterUi::create_screen_lock_visibility_control(lv_obj_t* input, int y) {
    auto control = std::make_unique<PasswordVisibilityControl>();
    configure_password_visibility_control(control.get(), input, y, 36);
    screen_lock_visibility_controls_.push_back(std::move(control));
}

void StarterUi::focus_screen_lock_input(lv_obj_t* input) {
    if (screen_lock_keyboard_ == nullptr || input == nullptr) {
        return;
    }
    if (screen_lock_pin_input_ != nullptr) {
        lv_obj_remove_state(screen_lock_pin_input_, LV_STATE_FOCUSED);
    }
    if (screen_lock_pin_confirm_input_ != nullptr) {
        lv_obj_remove_state(screen_lock_pin_confirm_input_, LV_STATE_FOCUSED);
    }
    lv_obj_add_state(input, LV_STATE_FOCUSED);
    lv_obj_send_event(input, LV_EVENT_FOCUSED, nullptr);
    lv_keyboard_set_textarea(screen_lock_keyboard_, input);
}

void StarterUi::show_screen_lock_pin_setup() {
    clear_screen();
    screen_id_ = "screen_lock_pin_setup";
    screen_lock_pin_setup_visible_ = true;
    create_title("Set Screen Lock PIN", 6);

    lv_obj_t* const guidance = lv_label_create(lv_screen_active());
    lv_obj_set_width(guidance, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(guidance, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(guidance, LV_LABEL_LONG_WRAP);
    lv_label_set_text(guidance, "Choose a PIN between 4 and 10 digits.");
    lv_obj_align(guidance, LV_ALIGN_TOP_MID, 0, 36);
    UiTheme::set_role(guidance, UiThemeRole::DimText);

    screen_lock_pin_input_ = lv_textarea_create(lv_screen_active());
    configure_screen_lock_input(screen_lock_pin_input_, "New PIN", 72);
    create_screen_lock_visibility_control(screen_lock_pin_input_, 72);
    screen_lock_pin_confirm_input_ = lv_textarea_create(lv_screen_active());
    configure_screen_lock_input(screen_lock_pin_confirm_input_, "Confirm PIN", 120);
    create_screen_lock_visibility_control(screen_lock_pin_confirm_input_, 120);

    screen_lock_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(screen_lock_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(screen_lock_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(screen_lock_status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(screen_lock_status_label_, LV_ALIGN_TOP_MID, 0, 166);
    UiTheme::set_role(screen_lock_status_label_, UiThemeRole::DimText);

    create_button("Save PIN", 208, "__screen_lock_submit_pin");
    create_button("Back", 258, "__back");
    screen_lock_keyboard_ = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_mode(screen_lock_keyboard_, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(screen_lock_keyboard_, screen_width(), screen_height() - 316);
    lv_obj_align(screen_lock_keyboard_, LV_ALIGN_TOP_MID, 0, 316);
    lv_obj_add_event_cb(screen_lock_keyboard_, screen_lock_keyboard_callback, LV_EVENT_READY, this);
    lv_obj_add_event_cb(screen_lock_keyboard_, screen_lock_keyboard_callback, LV_EVENT_CANCEL, this);
    focus_screen_lock_input(screen_lock_pin_input_);
}

void StarterUi::show_screen_lock_disable() {
    clear_screen();
    screen_id_ = "screen_lock_disable";
    screen_lock_disable_visible_ = true;
    create_title("Disable Screen Lock", 6);

    lv_obj_t* const guidance = lv_label_create(lv_screen_active());
    lv_obj_set_width(guidance, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(guidance, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(guidance, LV_LABEL_LONG_WRAP);
    lv_label_set_text(guidance, "Enter the current PIN to disable screen lock.");
    lv_obj_align(guidance, LV_ALIGN_TOP_MID, 0, 42);
    UiTheme::set_role(guidance, UiThemeRole::DimText);

    screen_lock_pin_input_ = lv_textarea_create(lv_screen_active());
    configure_screen_lock_input(screen_lock_pin_input_, "Current PIN", 94);
    create_screen_lock_visibility_control(screen_lock_pin_input_, 94);
    screen_lock_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(screen_lock_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(screen_lock_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(screen_lock_status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(screen_lock_status_label_, LV_ALIGN_TOP_MID, 0, 142);
    UiTheme::set_role(screen_lock_status_label_, UiThemeRole::DimText);

    create_button("Disable screen lock", 186, "__screen_lock_submit_disable");
    create_button("Back", 238, "__back");
    screen_lock_keyboard_ = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_mode(screen_lock_keyboard_, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(screen_lock_keyboard_, screen_width(), screen_height() - 296);
    lv_obj_align(screen_lock_keyboard_, LV_ALIGN_TOP_MID, 0, 296);
    lv_obj_add_event_cb(screen_lock_keyboard_, screen_lock_keyboard_callback, LV_EVENT_READY, this);
    lv_obj_add_event_cb(screen_lock_keyboard_, screen_lock_keyboard_callback, LV_EVENT_CANCEL, this);
    focus_screen_lock_input(screen_lock_pin_input_);
}

void StarterUi::show_screen_lock() {
    clear_screen();
    screen_id_ = "screen_lock";
    screen_lock_visible_ = true;
    create_title("Screen Locked", 8);

    lv_obj_t* const guidance = lv_label_create(lv_screen_active());
    lv_obj_set_width(guidance, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(guidance, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(guidance, LV_LABEL_LONG_WRAP);
    lv_label_set_text(guidance, "Enter your PIN to unlock MicroPanel Touch.");
    lv_obj_align(guidance, LV_ALIGN_TOP_MID, 0, 48);
    UiTheme::set_role(guidance, UiThemeRole::DimText);

    screen_lock_pin_input_ = lv_textarea_create(lv_screen_active());
    configure_screen_lock_input(screen_lock_pin_input_, "PIN", 104);
    create_screen_lock_visibility_control(screen_lock_pin_input_, 104);
    screen_lock_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(screen_lock_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(screen_lock_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(screen_lock_status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(screen_lock_status_label_, LV_ALIGN_TOP_MID, 0, 154);
    UiTheme::set_role(screen_lock_status_label_, UiThemeRole::DimText);

    create_button("Unlock", 198, "__screen_lock_submit_unlock");
    screen_lock_keyboard_ = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_mode(screen_lock_keyboard_, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(screen_lock_keyboard_, screen_width(), screen_height() - 258);
    lv_obj_align(screen_lock_keyboard_, LV_ALIGN_TOP_MID, 0, 258);
    lv_obj_add_event_cb(screen_lock_keyboard_, screen_lock_keyboard_callback, LV_EVENT_READY, this);
    lv_obj_add_event_cb(screen_lock_keyboard_, screen_lock_keyboard_callback, LV_EVENT_CANCEL, this);
    focus_screen_lock_input(screen_lock_pin_input_);
}

void StarterUi::submit_screen_lock_pin_setup() {
    if (!screen_lock_pin_setup_visible_ || screen_lock_pin_input_ == nullptr ||
        screen_lock_pin_confirm_input_ == nullptr || screen_lock_status_label_ == nullptr) {
        return;
    }
    const std::string_view pin(lv_textarea_get_text(screen_lock_pin_input_));
    const std::string_view confirm(lv_textarea_get_text(screen_lock_pin_confirm_input_));
    if (!platform::screen_lock_pin_is_valid(pin)) {
        lv_label_set_text(screen_lock_status_label_, "PIN must contain 4 to 10 digits.");
        UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        return;
    }
    if (pin.size() != confirm.size() ||
        std::char_traits<char>::compare(pin.data(), confirm.data(), pin.size()) != 0) {
        lv_label_set_text(screen_lock_status_label_, "PIN entries do not match.");
        UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        lv_textarea_set_text(screen_lock_pin_confirm_input_, "");
        focus_screen_lock_input(screen_lock_pin_confirm_input_);
        return;
    }
    std::string diagnostic;
    const bool saved = set_screen_lock_pin_ && set_screen_lock_pin_(pin, &diagnostic);
    lv_textarea_set_text(screen_lock_pin_input_, "");
    lv_textarea_set_text(screen_lock_pin_confirm_input_, "");
    if (!saved) {
        const std::string status = "Unable to save PIN: " + diagnostic;
        lv_label_set_text(screen_lock_status_label_, status.c_str());
        UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        return;
    }
    show_screen_lock_settings();
}

void StarterUi::submit_screen_lock_unlock() {
    if (!screen_lock_visible_ || screen_lock_pin_input_ == nullptr ||
        screen_lock_status_label_ == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto retry_delay = screen_lock_attempt_limiter_.remaining(now);
    if (retry_delay.count() != 0) {
        lv_textarea_set_text(screen_lock_pin_input_, "");
        const std::string status = "Too many attempts. Try again in " +
                                   std::to_string(retry_delay.count()) + " seconds.";
        lv_label_set_text(screen_lock_status_label_, status.c_str());
        UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        focus_screen_lock_input(screen_lock_pin_input_);
        return;
    }
    const std::string_view pin(lv_textarea_get_text(screen_lock_pin_input_));
    const bool verified = verify_screen_lock_pin_ && verify_screen_lock_pin_(pin);
    lv_textarea_set_text(screen_lock_pin_input_, "");
    if (!verified) {
        const auto delay = screen_lock_attempt_limiter_.record_failure(now);
        const std::string status = delay.count() == 0
            ? "Incorrect PIN. Try again."
            : "Too many attempts. Try again in " + std::to_string(delay.count()) +
                  " seconds.";
        lv_label_set_text(screen_lock_status_label_, status.c_str());
        UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        focus_screen_lock_input(screen_lock_pin_input_);
        return;
    }
    screen_lock_attempt_limiter_.record_success();
    if (set_screen_lock_session_) {
        set_screen_lock_session_(false);
    }
    show_root();
}

void StarterUi::submit_screen_lock_disable() {
    if (!screen_lock_disable_visible_ || screen_lock_pin_input_ == nullptr ||
        screen_lock_status_label_ == nullptr) {
        return;
    }
    const std::string_view pin(lv_textarea_get_text(screen_lock_pin_input_));
    const bool verified = verify_screen_lock_pin_ && verify_screen_lock_pin_(pin);
    lv_textarea_set_text(screen_lock_pin_input_, "");
    if (!verified) {
        lv_label_set_text(screen_lock_status_label_, "Incorrect PIN. Try again.");
        UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        focus_screen_lock_input(screen_lock_pin_input_);
        return;
    }
    std::string diagnostic;
    if (!set_screen_lock_enabled_ || !set_screen_lock_enabled_(false, &diagnostic)) {
        const std::string status = "Unable to disable screen lock: " + diagnostic;
        lv_label_set_text(screen_lock_status_label_, status.c_str());
        UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        return;
    }
    if (set_screen_lock_session_) {
        set_screen_lock_session_(false);
    }
    show_screen_lock_settings();
}

void StarterUi::show_touch_calibration() {
    clear_screen();
    screen_id_ = "touch_calibration";
    touch_calibration_visible_ = true;
    create_title("Touch Calibration", screen_height() > screen_width() ? 8 : 14);

    touch_calibration_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(touch_calibration_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(touch_calibration_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(touch_calibration_status_label_, LV_LABEL_LONG_WRAP);
    UiTheme::set_role(touch_calibration_status_label_, UiThemeRole::DimText);
    lv_obj_align(touch_calibration_status_label_, LV_ALIGN_TOP_MID, 0, 42);

    touch_calibration_target_ = lv_button_create(lv_screen_active());
    lv_obj_set_size(touch_calibration_target_, kTouchCalibrationTargetDiameter,
                    kTouchCalibrationTargetDiameter);
    lv_obj_t* const target_label = lv_label_create(touch_calibration_target_);
    lv_obj_center(target_label);

    const int control_gap = 8;
    const int control_width =
        (screen_width() - 2 * kHorizontalMargin - control_gap) / 2;
    const int control_y = screen_height() - button_height() - 12;
    touch_calibration_reset_button_ = create_button(
        "Reset default", kHorizontalMargin, control_y, control_width, button_height(),
        "__reset_touch_calibration");
    touch_calibration_cancel_button_ = create_button(
        "Cancel", kHorizontalMargin + control_width + control_gap, control_y,
        control_width, button_height(), "__back");
    const int radius = kTouchCalibrationTargetDiameter / 2;
    const int left = radius + 4;
    const int right = screen_width() - radius - 5;
    const int top = radius + 74;
    const int bottom = screen_height() - button_height() - radius - 24;
    touch_calibration_targets_ = {
        {left, top}, {right, top}, {right, bottom}, {left, bottom},
        {screen_width() / 2, screen_height() / 2},
    };
    if (apply_touch_calibration_ == nullptr || reset_touch_calibration_ == nullptr ||
        logical_to_native_point_ == nullptr) {
        touch_calibration_complete_ = true;
        lv_obj_add_flag(touch_calibration_target_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(touch_calibration_status_label_, "Touch calibration is unavailable.");
        lv_obj_add_state(touch_calibration_reset_button_, LV_STATE_DISABLED);
        if (lv_obj_get_child_count(touch_calibration_cancel_button_) != 0U) {
            lv_label_set_text(lv_obj_get_child(touch_calibration_cancel_button_, 0U), "Back");
        }
        return;
    }
    update_touch_calibration_target();
}

void StarterUi::update_touch_calibration_target() {
    if (!touch_calibration_visible_ || touch_calibration_complete_ ||
        touch_calibration_status_label_ == nullptr || touch_calibration_target_ == nullptr ||
        touch_calibration_target_index_ >= touch_calibration_targets_.size()) {
        return;
    }
    const platform::TouchPoint target = touch_calibration_targets_[touch_calibration_target_index_];
    const int radius = kTouchCalibrationTargetDiameter / 2;
    lv_obj_set_pos(touch_calibration_target_, target.x - radius, target.y - radius);
    if (lv_obj_get_child_count(touch_calibration_target_) != 0U) {
        const std::string target_number = std::to_string(touch_calibration_target_index_ + 1U);
        lv_label_set_text(lv_obj_get_child(touch_calibration_target_, 0U), target_number.c_str());
    }
    const std::string status = "Tap the center of target " +
                               std::to_string(touch_calibration_target_index_ + 1U) + " of 5.";
    lv_label_set_text(touch_calibration_status_label_, status.c_str());
    UiTheme::set_role(touch_calibration_status_label_, UiThemeRole::DimText);
}

void StarterUi::accept_touch_calibration_sample(const core::TouchCalibrationRawSample& sample) {
    if (!touch_calibration_visible_ || touch_calibration_complete_ ||
        touch_calibration_target_index_ >= touch_calibration_targets_.size() ||
        touch_calibration_status_label_ == nullptr || logical_to_native_point_ == nullptr ||
        apply_touch_calibration_ == nullptr) {
        return;
    }
    const platform::TouchPoint target = touch_calibration_targets_[touch_calibration_target_index_];
    if (touch_calibration_reset_confirmed_) {
        touch_calibration_reset_confirmed_ = false;
        if (touch_calibration_reset_button_ != nullptr &&
            lv_obj_get_child_count(touch_calibration_reset_button_) != 0U) {
            lv_label_set_text(lv_obj_get_child(touch_calibration_reset_button_, 0U),
                              "Reset default");
        }
    }
    if (std::abs(sample.screen_x - target.x) > kTouchCalibrationTargetAcceptRadius ||
        std::abs(sample.screen_y - target.y) > kTouchCalibrationTargetAcceptRadius) {
        lv_label_set_text(touch_calibration_status_label_, "Tap the numbered target, not the buttons.");
        UiTheme::set_role(touch_calibration_status_label_, UiThemeRole::ErrorText);
        return;
    }
    touch_calibration_samples_.push_back({{sample.raw_x, sample.raw_y},
                                          logical_to_native_point_(target)});
    ++touch_calibration_target_index_;
    if (touch_calibration_target_index_ < touch_calibration_targets_.size()) {
        update_touch_calibration_target();
        return;
    }

    std::string diagnostic;
    if (!apply_touch_calibration_(touch_calibration_samples_, &diagnostic)) {
        touch_calibration_samples_.clear();
        touch_calibration_target_index_ = 0U;
        const std::string message = "Calibration failed: " + diagnostic + ". Start again.";
        update_touch_calibration_target();
        lv_label_set_text(touch_calibration_status_label_, message.c_str());
        UiTheme::set_role(touch_calibration_status_label_, UiThemeRole::ErrorText);
        return;
    }
    touch_calibration_complete_ = true;
    lv_obj_add_flag(touch_calibration_target_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(touch_calibration_status_label_,
                      "Calibration saved and active. Test the keypad now.");
    UiTheme::set_role(touch_calibration_status_label_, UiThemeRole::SuccessText);
    if (touch_calibration_cancel_button_ != nullptr &&
        lv_obj_get_child_count(touch_calibration_cancel_button_) != 0U) {
        lv_label_set_text(lv_obj_get_child(touch_calibration_cancel_button_, 0U), "Back");
    }
}

void StarterUi::reset_touch_calibration() {
    if (!touch_calibration_visible_ || touch_calibration_status_label_ == nullptr ||
        touch_calibration_reset_button_ == nullptr || reset_touch_calibration_ == nullptr) {
        return;
    }
    if (!touch_calibration_reset_confirmed_) {
        touch_calibration_reset_confirmed_ = true;
        lv_label_set_text(touch_calibration_status_label_,
                          "Tap Reset default again to restore the factory mapping.");
        UiTheme::set_role(touch_calibration_status_label_, UiThemeRole::ErrorText);
        if (lv_obj_get_child_count(touch_calibration_reset_button_) != 0U) {
            lv_label_set_text(lv_obj_get_child(touch_calibration_reset_button_, 0U),
                              "Confirm reset");
        }
        return;
    }

    std::string diagnostic;
    if (!reset_touch_calibration_(&diagnostic)) {
        touch_calibration_reset_confirmed_ = false;
        const std::string message = "Unable to reset calibration: " + diagnostic;
        lv_label_set_text(touch_calibration_status_label_, message.c_str());
        UiTheme::set_role(touch_calibration_status_label_, UiThemeRole::ErrorText);
        if (lv_obj_get_child_count(touch_calibration_reset_button_) != 0U) {
            lv_label_set_text(lv_obj_get_child(touch_calibration_reset_button_, 0U),
                              "Reset default");
        }
        return;
    }

    touch_calibration_reset_confirmed_ = false;
    touch_calibration_complete_ = true;
    touch_calibration_samples_.clear();
    lv_obj_add_flag(touch_calibration_target_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(touch_calibration_status_label_,
                      "Factory mapping restored. Reopen this screen to calibrate.");
    UiTheme::set_role(touch_calibration_status_label_, UiThemeRole::SuccessText);
    if (lv_obj_get_child_count(touch_calibration_reset_button_) != 0U) {
        lv_label_set_text(lv_obj_get_child(touch_calibration_reset_button_, 0U),
                          "Reset default");
    }
    if (touch_calibration_cancel_button_ != nullptr &&
        lv_obj_get_child_count(touch_calibration_cancel_button_) != 0U) {
        lv_label_set_text(lv_obj_get_child(touch_calibration_cancel_button_, 0U), "Back");
    }
}

void StarterUi::show_placeholder(const std::string& title) {
    clear_screen();
    create_title(title);
    lv_obj_t* const label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Coming in this Sprint 1 vertical slice");
    UiTheme::set_role(label, UiThemeRole::DimText);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -16);
    create_button("Back", screen_height() - button_height() - 12,
                  "__back");
}

void StarterUi::show_parent_menu() {
    const auto parent_id = navigation_.back();
    if (!parent_id.has_value() || parent_id->empty()) {
        show_root();
        return;
    }

    const StarterModule* const parent = config_.find(*parent_id);
    if (parent == nullptr || parent->type != "menu") {
        show_root();
        return;
    }
    show_menu(*parent);
}

void StarterUi::activate(const std::string& id) {
    // The lock gate is intentionally not part of navigation history.  Do not
    // let a back/root control (including the development control socket) walk
    // around it while a locked session is active.
    if (screen_lock_visible_ && id != "__screen_lock_submit_unlock") {
        return;
    }
    if (id == "__root") {
        show_root();
        return;
    }
    if (id == "__apply_display_standby") {
        apply_display_standby_settings();
        return;
    }
    if (id == "__screen_lock_set_pin") {
        show_screen_lock_pin_setup();
        return;
    }
    if (id == "__screen_lock_submit_pin") {
        submit_screen_lock_pin_setup();
        return;
    }
    if (id == "__screen_lock_enable") {
        const auto settings = screen_lock_settings_provider_ ? screen_lock_settings_provider_()
                                                             : std::nullopt;
        if (!settings.has_value() || !settings->configured) {
            if (screen_lock_status_label_ != nullptr) {
                lv_label_set_text(screen_lock_status_label_, "Set a PIN before enabling screen lock.");
                UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
            }
            return;
        }
        std::string diagnostic;
        if (set_screen_lock_enabled_ && set_screen_lock_enabled_(true, &diagnostic)) {
            show_screen_lock_settings();
        } else if (screen_lock_status_label_ != nullptr) {
            const std::string status = "Unable to enable screen lock: " + diagnostic;
            lv_label_set_text(screen_lock_status_label_, status.c_str());
            UiTheme::set_role(screen_lock_status_label_, UiThemeRole::ErrorText);
        }
        return;
    }
    if (id == "__screen_lock_disable") {
        show_screen_lock_disable();
        return;
    }
    if (id == "__screen_lock_submit_disable") {
        submit_screen_lock_disable();
        return;
    }
    if (id == "__screen_lock_now") {
        if (set_screen_lock_session_) {
            set_screen_lock_session_(true);
        }
        show_screen_lock();
        return;
    }
    if (id == "__screen_lock_submit_unlock") {
        submit_screen_lock_unlock();
        return;
    }
    if (id == "__back") {
        if (screen_lock_pin_setup_visible_ || screen_lock_disable_visible_) {
            show_screen_lock_settings();
            return;
        }
        if (network_apply_pending_) {
            if (network_result_label_ != nullptr) {
                lv_label_set_text(network_result_label_,
                                  "Applying network settings; wait for the result.");
                UiTheme::set_role(network_result_label_, UiThemeRole::DimText);
            } else if (ip_status_label_ != nullptr) {
                lv_label_set_text(ip_status_label_, "Applying network settings; wait for the result.");
                UiTheme::set_role(ip_status_label_, UiThemeRole::DimText);
            }
            return;
        }
        if (system_update_pending_) {
            if (system_update_result_label_ != nullptr) {
                lv_label_set_text(system_update_result_label_,
                                  "Installing update; wait for candidate reboot or the final result.");
                UiTheme::set_role(system_update_result_label_, UiThemeRole::DimText);
            }
            return;
        }
        if (action_runner_visible_ && action_runner_running_) {
            if (cancel_action_) {
                cancel_action_();
            }
            if (action_runner_status_label_ != nullptr) {
                action_runner_status_text_ = "Cancelling action...";
                lv_label_set_text(action_runner_status_label_, action_runner_status_text_.c_str());
                UiTheme::set_role(action_runner_status_label_, UiThemeRole::DimText);
            }
            return;
        }
        show_parent_menu();
        return;
    }
    if (id == "netinfo") {
        navigation_.enter_leaf();
        show_network_info();
        return;
    }
    if (id == "netsettings") {
        navigation_.enter_leaf();
        show_ip_settings();
        return;
    }
    if (id == "software_update") {
        navigation_.enter_leaf();
        show_system_update();
        return;
    }
    if (id == "wifi") {
        navigation_.enter_leaf();
        show_wifi();
        return;
    }
    if (id == "wifi_password_demo") {
        navigation_.enter_leaf();
        show_wifi_password_demo();
        return;
    }
    if (id == "theme_select") {
        navigation_.enter_leaf();
        theme_message_.clear();
        show_theme_selection();
        return;
    }
    if (id == "progress_demo") {
        navigation_.enter_leaf();
        show_progress_demo();
        return;
    }
    if (id == "action_runner_demo") {
        navigation_.enter_leaf();
        show_action_runner_demo();
        return;
    }
    if (id == "slider_demo") {
        navigation_.enter_leaf();
        show_slider_demo();
        return;
    }
    if (id == "brightness") {
        navigation_.enter_leaf();
        show_display_brightness();
        return;
    }
    if (id == "display_standby") {
        navigation_.enter_leaf();
        show_display_standby();
        return;
    }
    if (id == "screen_lock") {
        navigation_.enter_leaf();
        show_screen_lock_settings();
        return;
    }
    if (id == "touch_calibration") {
        navigation_.enter_leaf();
        show_touch_calibration();
        return;
    }
    if (id == "__reset_touch_calibration") {
        reset_touch_calibration();
        return;
    }
    if (id == "__validate_ip") {
        validate_ip_settings();
        return;
    }
    if (id == "__check_system_update") {
        if (system_update_pending_) {
            return;
        }
        if (!request_system_update_) {
            show_system_update_result("System update broker is unavailable; no update was started.",
                                      false, false);
            return;
        }
        const std::uint64_t request_id = next_system_update_request_id_++;
        std::string diagnostic;
        if (!request_system_update_(
                request_id, {std::string{core::kSystemUpdateUsbSource}}, &diagnostic)) {
            show_system_update_result(diagnostic.empty()
                                          ? "Unable to start the USB update; no slot was changed."
                                          : diagnostic,
                                      false, false);
            return;
        }
        show_system_update_result(
            "Verifying USB payload and writing the inactive slot. Do not remove power."
            " The panel will reboot into the candidate only after verification succeeds.",
            false, true);
        system_update_pending_ = true;
        system_update_request_id_ = request_id;
        return;
    }
    if (id == "__wifi_scan") {
        request_wifi_scan();
        return;
    }
    if (id == "__cancel_action") {
        if (action_runner_visible_ && action_runner_running_ && cancel_action_) {
            cancel_action_();
            if (action_runner_status_label_ != nullptr) {
                action_runner_status_text_ = "Cancelling action...";
                lv_label_set_text(action_runner_status_label_, action_runner_status_text_.c_str());
                UiTheme::set_role(action_runner_status_label_, UiThemeRole::DimText);
            }
        }
        return;
    }
    constexpr const char* kThemeActionPrefix = "__theme:";
    if (id.rfind(kThemeActionPrefix, 0) == 0) {
        const std::string requested = id.substr(std::char_traits<char>::length(kThemeActionPrefix));
        std::string diagnostic;
        if (select_theme_ == nullptr || !select_theme_(requested, &diagnostic)) {
            theme_message_ = "Unable to apply " + requested + ": " + diagnostic;
        } else {
            theme_message_.clear();
        }
        show_theme_selection();
        return;
    }
    const StarterModule* const module = config_.find(id);
    if (module != nullptr && module->type == "menu") {
        navigation_.enter_menu(module->id);
        show_menu(*module);
        return;
    }
    if (module != nullptr) {
        navigation_.enter_leaf();
        screen_id_ = module->id;
        show_placeholder(module->title);
        return;
    }
    navigation_.enter_leaf();
    screen_id_ = id;
    show_placeholder(id);
}

void StarterUi::queue_action(const std::string& id) {
    auto pending = std::make_unique<PendingAction>(PendingAction{this, id});
    PendingAction* const raw_action = pending.get();
    if (lv_async_call(deferred_action_callback, raw_action) != LV_RESULT_OK) {
        // Allocation failure is exceptionally rare; retain the prior behaviour
        // rather than dropping a user action entirely.
        activate(id);
        return;
    }
    pending_actions_.push_back(std::move(pending));
}

void StarterUi::queue_tap(const core::UiControlCommand& command,
                          std::shared_ptr<std::promise<core::UiControlResponse>> completion) {
    if (completion == nullptr) {
        return;
    }
    const auto fail = [&completion](std::string error) {
        try {
            completion->set_value({false, {}, {}, {}, false, std::move(error)});
        } catch (const std::future_error&) {
            // The control peer already disconnected.
        }
    };
    if (synthetic_touch_ == nullptr) {
        fail("synthetic tap is unavailable without a control pointer device");
        return;
    }
    std::string diagnostic;
    if (!synthetic_touch_->tap(command.x, command.y, &diagnostic)) {
        fail(std::move(diagnostic));
        return;
    }

    auto pending = std::make_unique<PendingTapReply>(PendingTapReply{this, completion});
    PendingTapReply* const raw_reply = pending.get();
    // A click can queue a zero-delay LVGL screen action. Its timer is inserted
    // newest-first, so settle on the next UI turn rather than responding ahead
    // of that action.
    raw_reply->settlement_timer =
        lv_timer_create(deferred_tap_reply_timer_callback, 1U, raw_reply);
    if (raw_reply->settlement_timer == nullptr) {
        fail("unable to queue synthetic tap completion");
        return;
    }
    lv_timer_set_repeat_count(raw_reply->settlement_timer, 1);
    pending_tap_replies_.push_back(std::move(pending));
}

void StarterUi::queue_text(const core::UiControlCommand& command,
                           std::shared_ptr<std::promise<core::UiControlResponse>> completion) {
    if (completion == nullptr) {
        return;
    }
    const auto fail = [&completion](std::string error) {
        try {
            completion->set_value({false, {}, {}, {}, false, std::move(error)});
        } catch (const std::future_error&) {
            // The control peer already disconnected.
        }
    };
    if (wifi_password_visible_ || screen_lock_visible_ || screen_lock_pin_setup_visible_ ||
        screen_lock_disable_visible_) {
        fail("text injection is forbidden for password fields");
        return;
    }
    if (!ip_settings_visible_ || keyboard_ == nullptr || synthetic_keypad_ == nullptr ||
        ip_mode_dropdown_ == nullptr || lv_dropdown_get_selected(ip_mode_dropdown_) == 0U) {
        fail("text injection is available only for visible IP settings fields");
        return;
    }

    lv_obj_t* target = nullptr;
    if (command.target == "ip_address") {
        target = ip_address_input_;
    } else if (command.target == "netmask") {
        target = netmask_input_;
    } else if (command.target == "gateway" ||
               (command.target == "lease_start" &&
                lv_dropdown_get_selected(ip_mode_dropdown_) == 2U)) {
        target = gateway_input_;
    } else if (command.target == "lease_end" && lv_dropdown_get_selected(ip_mode_dropdown_) == 2U) {
        target = lease_end_input_;
    } else {
        fail("text field is not approved for control injection");
        return;
    }
    if (target == nullptr || lv_keyboard_get_textarea(keyboard_) != target ||
        !synthetic_keypad_->is_focused(target)) {
        fail("text field is not visibly focused");
        return;
    }
    const bool allow_dot = command.target == "ip_address" || command.target == "gateway" ||
                           command.target == "lease_start" || command.target == "netmask" ||
                           command.target == "lease_end";
    const bool accepted = std::all_of(command.text.begin(), command.text.end(), [allow_dot](char character) {
        return (character >= '0' && character <= '9') || (allow_dot && character == '.');
    });
    if (!accepted) {
        fail("text contains characters not accepted by the focused field");
        return;
    }

    std::string diagnostic;
    if (!synthetic_keypad_->type(command.text, &diagnostic)) {
        fail(std::move(diagnostic));
        return;
    }
    settle_render();
    try {
        completion->set_value(state_response());
    } catch (const std::future_error&) {
        // The control peer already disconnected.
    }
}

core::UiControlResponse StarterUi::state_response() const {
    return {true, screen_id_, navigation_.menu_path(), {}, false, {}};
}

void StarterUi::settle_render() const {
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(lv_display_get_default());
}

void StarterUi::append_widget_snapshots(lv_obj_t* object, std::int32_t parent_id,
                                        bool ancestor_redacted, std::uint32_t* next_id,
                                        core::UiControlResponse* response) const {
    if (object == nullptr) {
        return;
    }
    if (response->widgets.size() >= kMaximumWidgetSnapshots) {
        response->widget_tree_truncated = true;
        return;
    }
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    const bool redacted = ancestor_redacted || lv_obj_check_type(object, &lv_textarea_class);
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    core::UiWidgetSnapshot snapshot;
    snapshot.id = *next_id;
    snapshot.parent_id = parent_id;
    snapshot.type = widget_type(object);
    snapshot.x = area.x1;
    snapshot.y = area.y1;
    snapshot.width = lv_area_get_width(&area);
    snapshot.height = lv_area_get_height(&area);
    snapshot.redacted = redacted;
    if (redacted) {
        snapshot.text = "<redacted>";
    } else if (lv_obj_check_type(object, &lv_label_class)) {
        snapshot.text = bounded_text(lv_label_get_text(object), &snapshot.text_truncated);
    }
    response->widgets.push_back(std::move(snapshot));
    const std::uint32_t this_id = (*next_id)++;

    // Textareas own their rendered text labels. Never traverse them: both
    // public IP values and future secrets stay out of control captures.
    if (redacted) {
        return;
    }
    const std::uint32_t child_count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0U; index < child_count; ++index) {
        append_widget_snapshots(lv_obj_get_child(object, index), static_cast<std::int32_t>(this_id),
                                false, next_id, response);
        if (response->widget_tree_truncated) {
            return;
        }
    }
}

core::UiControlResponse StarterUi::handle_control(const core::UiControlCommand& command) {
    if (command.type == core::UiControlCommandType::State) {
        settle_render();
        return state_response();
    }
    if (command.type == core::UiControlCommandType::CaptureTree ||
        command.type == core::UiControlCommandType::CaptureFrame) {
        settle_render();
        core::UiControlResponse response = state_response();
        if (command.type == core::UiControlCommandType::CaptureTree) {
            std::uint32_t next_id = 0U;
            append_widget_snapshots(lv_screen_active(), -1, false, &next_id, &response);
        } else if (!frame_capture_) {
            return {false, {}, {}, {}, false, "frame capture is unavailable"};
        } else {
            std::string diagnostic;
            response.frame_capture = frame_capture_(&diagnostic);
            if (!response.frame_capture.has_value()) {
                return {false, {}, {}, {}, false, "frame capture failed: " + diagnostic};
            }
        }
        return response;
    }
    if (command.type == core::UiControlCommandType::Back) {
        activate("__back");
        settle_render();
        return state_response();
    }
    return {false, {}, {}, {}, false, "control navigation currently requires --legacy-config"};
}

void StarterUi::focus_ip_input(lv_obj_t* input) {
    if (!ip_settings_visible_ || keyboard_ == nullptr || input == nullptr) {
        return;
    }
    lv_obj_remove_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    if (ip_address_input_ != nullptr) {
        lv_obj_remove_state(ip_address_input_, LV_STATE_FOCUSED);
    }
    if (netmask_input_ != nullptr) {
        lv_obj_remove_state(netmask_input_, LV_STATE_FOCUSED);
    }
    if (gateway_input_ != nullptr) {
        lv_obj_remove_state(gateway_input_, LV_STATE_FOCUSED);
    }
    if (lease_end_input_ != nullptr) {
        lv_obj_remove_state(lease_end_input_, LV_STATE_FOCUSED);
    }
    lv_obj_add_state(input, LV_STATE_FOCUSED);
    // Adding the visual focused state alone does not emit LV_EVENT_FOCUSED,
    // which is what starts LVGL's cursor-blink animation.
    lv_obj_send_event(input, LV_EVENT_FOCUSED, nullptr);
    lv_keyboard_set_textarea(keyboard_, input);
    if (synthetic_keypad_ != nullptr) {
        synthetic_keypad_->focus(input, nullptr);
    }
}

void StarterUi::dismiss_keyboard() {
    if (keyboard_ == nullptr) {
        return;
    }
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    if (ip_address_input_ != nullptr) {
        lv_obj_remove_state(ip_address_input_, LV_STATE_FOCUSED);
    }
    if (netmask_input_ != nullptr) {
        lv_obj_remove_state(netmask_input_, LV_STATE_FOCUSED);
    }
    if (gateway_input_ != nullptr) {
        lv_obj_remove_state(gateway_input_, LV_STATE_FOCUSED);
    }
    if (lease_end_input_ != nullptr) {
        lv_obj_remove_state(lease_end_input_, LV_STATE_FOCUSED);
    }
}

void StarterUi::validate_ip_settings() {
    if (!ip_settings_visible_ || ip_status_label_ == nullptr || ip_mode_dropdown_ == nullptr) {
        return;
    }
    const std::uint16_t mode = lv_dropdown_get_selected(ip_mode_dropdown_);
    const bool static_mode = mode == 1U;
    const bool dhcp_server_mode = mode == 2U;
    core::NetworkOperation operation{core::DhcpOperation{static_ip_interface_}};
    std::string pending_text = "Applying DHCP client on " + static_ip_interface_ + "...";
    if (static_mode) {
        if (ip_address_input_ == nullptr || gateway_input_ == nullptr || netmask_input_ == nullptr) {
            return;
        }
        const auto prefix_length =
            core::prefix_length_from_ipv4_netmask(lv_textarea_get_text(netmask_input_));
        if (!prefix_length.has_value()) {
            lv_label_set_text(ip_status_label_, "Enter a contiguous IPv4 netmask.");
            UiTheme::set_role(ip_status_label_, UiThemeRole::ErrorText);
            return;
        }
        operation = core::StaticIpv4Operation{
            static_ip_interface_,
            {lv_textarea_get_text(ip_address_input_), *prefix_length,
             lv_textarea_get_text(gateway_input_)},
        };
        pending_text = "Applying Static IP on " + static_ip_interface_ + "...";
    } else if (dhcp_server_mode) {
        if (ip_address_input_ == nullptr || gateway_input_ == nullptr || netmask_input_ == nullptr ||
            lease_end_input_ == nullptr) {
            return;
        }
        const auto prefix_length =
            core::prefix_length_from_ipv4_netmask(lv_textarea_get_text(netmask_input_));
        if (!prefix_length.has_value()) {
            lv_label_set_text(ip_status_label_, "Enter a contiguous IPv4 netmask.");
            UiTheme::set_role(ip_status_label_, UiThemeRole::ErrorText);
            return;
        }
        operation = core::DhcpServerOperation{
            static_ip_interface_,
            {lv_textarea_get_text(ip_address_input_), *prefix_length,
             lv_textarea_get_text(gateway_input_), lv_textarea_get_text(lease_end_input_)},
        };
        pending_text = "Starting DHCP server on eth0; network access will change...";
    }
    const core::StaticIpValidationResult result = core::validate_network_operation(operation);
    if (!result.valid) {
        lv_label_set_text(ip_status_label_, result.message.c_str());
        UiTheme::set_role(ip_status_label_, UiThemeRole::ErrorText);
        return;
    }
    if (dhcp_server_mode && !dhcp_server_apply_confirmed_) {
        dhcp_server_apply_confirmed_ = true;
        lv_label_set_text(ip_status_label_,
                          "Confirm enable: disconnect eth0 from the normal LAN, then tap again.");
        UiTheme::set_role(ip_status_label_, UiThemeRole::ErrorText);
        if (ip_apply_button_ != nullptr && lv_obj_get_child_count(ip_apply_button_) != 0U) {
            lv_label_set_text(lv_obj_get_child(ip_apply_button_, 0U), "Confirm enable");
        }
        return;
    }
    if (!request_network_change_) {
        lv_label_set_text(ip_status_label_, result.message.c_str());
        UiTheme::set_role(ip_status_label_, UiThemeRole::SuccessText);
        return;
    }
    if (network_apply_pending_) {
        lv_label_set_text(ip_status_label_, "A network settings request is already in progress.");
        UiTheme::set_role(ip_status_label_, UiThemeRole::ErrorText);
        return;
    }

    const std::uint64_t request_id = next_network_apply_request_id_++;
    std::string diagnostic;
    if (!request_network_change_(request_id, operation, &diagnostic)) {
        const std::string message = diagnostic.empty()
            ? "Unable to request network settings; no network changes were made."
            : diagnostic;
        show_network_result(message, false, false);
        return;
    }
    show_network_result(pending_text, false, true);
    network_apply_pending_ = true;
    network_apply_request_id_ = request_id;
}

void StarterUi::request_wifi_scan() {
    if (!wifi_scan_visible_ || wifi_label_ == nullptr) {
        return;
    }
    wifi_scan_result_.reset();
    wifi_text_ = "Scanning Wi-Fi networks…";
    lv_label_set_text(wifi_label_, wifi_text_.c_str());
    if (wifi_spinner_ == nullptr) {
        wifi_spinner_ = lv_spinner_create(lv_screen_active());
        lv_obj_set_size(wifi_spinner_, 36, 36);
        lv_obj_align(wifi_spinner_, LV_ALIGN_CENTER, 0, 12);
    }
    if (request_wifi_scan_) {
        request_wifi_scan_();
    } else {
        wifi_text_ = "Wi-Fi scanning is unavailable.";
        lv_label_set_text(wifi_label_, wifi_text_.c_str());
    }
}

void StarterUi::refresh_network_info() {
    if (!network_info_visible_ || network_label_ == nullptr) {
        return;
    }
    std::ostringstream text;
    if (network_snapshot_.interfaces.empty()) {
        text << "Collecting interface state…";
    } else {
        for (const auto& interface : network_snapshot_.interfaces) {
            text << interface.name << "  " << interface.link_state
                 << (interface.carrier ? " / carrier" : " / no carrier") << '\n';
            text << "  " << (interface.ipv4_addresses.empty() ? "No IPv4 address"
                                                               : interface.ipv4_addresses.front())
                 << "   " << interface.mac_address << '\n';
        }
    }
    const std::string updated = text.str();
    if (updated != network_text_) {
        network_text_ = updated;
        lv_label_set_text(network_label_, network_text_.c_str());
    }
}

void StarterUi::refresh_wifi_scan() {
    if (!wifi_scan_visible_ || wifi_label_ == nullptr || !wifi_scan_result_.has_value()) {
        return;
    }
    if (wifi_spinner_ != nullptr) {
        lv_obj_delete(wifi_spinner_);
        wifi_spinner_ = nullptr;
    }

    std::ostringstream text;
    if (!wifi_scan_result_->diagnostic.empty()) {
        text << wifi_scan_result_->diagnostic;
    } else {
        const std::size_t visible_count = std::min<std::size_t>(wifi_scan_result_->access_points.size(), 6U);
        for (std::size_t index = 0; index < visible_count; ++index) {
            const auto& access_point = wifi_scan_result_->access_points[index];
            text << (access_point.active ? "* " : "  ")
                 << (access_point.ssid.empty() ? "<hidden network>" : access_point.ssid)
                 << "  " << access_point.signal_percent << "%"
                 << (access_point.security.empty() ? "" : "  locked") << '\n';
        }
        if (wifi_scan_result_->access_points.size() > visible_count) {
            text << "…and " << wifi_scan_result_->access_points.size() - visible_count << " more";
        }
    }
    std::string updated = text.str();
    constexpr std::size_t kMaximumWifiDiagnosticCharacters = 300U;
    if (updated.size() > kMaximumWifiDiagnosticCharacters) {
        updated.resize(kMaximumWifiDiagnosticCharacters - 1U);
        updated += "…";
    }
    if (updated != wifi_text_) {
        wifi_text_ = std::move(updated);
        lv_label_set_text(wifi_label_, wifi_text_.c_str());
    }
}

void StarterUi::update_progress_demo() {
    if (progress_bar_ == nullptr || progress_label_ == nullptr) {
        return;
    }

    const auto elapsed = std::min(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - progress_started_at_),
        std::chrono::duration_cast<std::chrono::milliseconds>(kProgressDemoDuration));
    const int percent = static_cast<int>(elapsed.count() * 100 /
                                         std::chrono::duration_cast<std::chrono::milliseconds>(
                                             kProgressDemoDuration).count());
    lv_bar_set_value(progress_bar_, percent, LV_ANIM_OFF);

    const auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    std::ostringstream text;
    if (elapsed >= kProgressDemoDuration) {
        text << "Complete\n30 seconds elapsed";
        UiTheme::set_role(progress_label_, UiThemeRole::SuccessText);
        if (progress_timer_ != nullptr) {
            lv_timer_pause(progress_timer_);
        }
    } else {
        text << "Running  " << percent << "%\nElapsed " << elapsed_seconds << " / 30 seconds";
    }
    std::string updated = text.str();
    if (updated != progress_text_) {
        progress_text_ = std::move(updated);
        lv_label_set_text(progress_label_, progress_text_.c_str());
    }
}

void StarterUi::update_action_runner_progress(const core::ActionProgress& progress) {
    if (!action_runner_visible_ || !action_runner_running_) {
        return;
    }
    if (action_runner_bar_ != nullptr && progress.progress_percent.has_value()) {
        lv_bar_set_value(action_runner_bar_, static_cast<int>(*progress.progress_percent), LV_ANIM_OFF);
    }
    if (action_runner_status_label_ != nullptr && progress.progress_percent.has_value()) {
        std::string text = "Running  " + std::to_string(*progress.progress_percent) + "%";
        if (progress.progress_is_estimated) {
            text += " (estimated)";
        }
        if (text != action_runner_status_text_) {
            action_runner_status_text_ = std::move(text);
            lv_label_set_text(action_runner_status_label_, action_runner_status_text_.c_str());
        }
    }
    if (action_runner_log_label_ != nullptr && !progress.log_tail.empty()) {
        std::ostringstream text;
        for (std::size_t index = 0; index < progress.log_tail.size(); ++index) {
            if (index != 0U) {
                text << '\n';
            }
            text << progress.log_tail[index];
        }
        const std::string updated = text.str();
        if (updated != action_runner_log_text_) {
            action_runner_log_text_ = updated;
            lv_label_set_text(action_runner_log_label_, action_runner_log_text_.c_str());
        }
    }
}

void StarterUi::show_action_runner_result(const core::ActionResult& result) {
    if (!action_runner_visible_ || action_runner_status_label_ == nullptr) {
        return;
    }
    action_runner_running_ = false;
    if (action_runner_cancel_button_ != nullptr) {
        lv_obj_add_flag(action_runner_cancel_button_, LV_OBJ_FLAG_HIDDEN);
    }
    if (action_progress_timer_ != nullptr) {
        lv_timer_pause(action_progress_timer_);
    }
    if (action_runner_bar_ != nullptr && result.progress_percent.has_value()) {
        lv_bar_set_value(action_runner_bar_, static_cast<int>(*result.progress_percent), LV_ANIM_OFF);
    }

    std::string text;
    UiThemeRole role = UiThemeRole::ErrorText;
    switch (result.status) {
        case core::ActionResultStatus::Succeeded:
            text = "Action succeeded";
            role = UiThemeRole::SuccessText;
            break;
        case core::ActionResultStatus::AssumedSucceeded:
            text = "Action completed (legacy no-log rule)";
            role = UiThemeRole::SuccessText;
            break;
        case core::ActionResultStatus::Cancelled:
            text = "Action cancelled";
            break;
        case core::ActionResultStatus::TimedOut:
            text = "Action timed out";
            break;
        case core::ActionResultStatus::Killed:
            text = "Action was killed";
            if (result.terminating_signal != 0) {
                text += " (signal " + std::to_string(result.terminating_signal) + ")";
            }
            break;
        case core::ActionResultStatus::StartFailed:
            text = "Action could not start";
            break;
        case core::ActionResultStatus::Failed:
            text = "Action failed";
            break;
    }
    if (!result.result_text.empty()) {
        text += "\n" + result.result_text;
    }
    if (!result.diagnostic.empty()) {
        text += "\n" + result.diagnostic;
    }
    action_runner_status_text_ = std::move(text);
    lv_label_set_text(action_runner_status_label_, action_runner_status_text_.c_str());
    UiTheme::set_role(action_runner_status_label_, role);

    if (action_runner_log_label_ != nullptr && !result.log_tail.empty()) {
        std::ostringstream log_text;
        for (std::size_t index = 0; index < result.log_tail.size(); ++index) {
            if (index != 0U) {
                log_text << '\n';
            }
            log_text << result.log_tail[index];
        }
        action_runner_log_text_ = log_text.str();
        lv_label_set_text(action_runner_log_label_, action_runner_log_text_.c_str());
    }
}

void StarterUi::update_slider_demo() {
    if (brightness_slider_ != nullptr && brightness_slider_label_ != nullptr) {
        const std::string updated = "Brightness  " +
                                    std::to_string(lv_slider_get_value(brightness_slider_)) + "%";
        if (updated != slider_brightness_text_) {
            slider_brightness_text_ = updated;
            lv_label_set_text(brightness_slider_label_, slider_brightness_text_.c_str());
        }
    }
    if (volume_slider_ != nullptr && volume_slider_label_ != nullptr) {
        const std::string updated = "Volume  " +
                                    std::to_string(lv_slider_get_value(volume_slider_)) + "%";
        if (updated != slider_volume_text_) {
            slider_volume_text_ = updated;
            lv_label_set_text(volume_slider_label_, slider_volume_text_.c_str());
        }
    }
}

void StarterUi::update_display_brightness_controls() {
    if (display_brightness_slider_ == nullptr || display_brightness_label_ == nullptr) {
        return;
    }
    lv_slider_set_value(display_brightness_slider_,
                        static_cast<int>(display_brightness_settings_.percent), LV_ANIM_OFF);
    const std::string updated = "Brightness: " +
                                std::to_string(display_brightness_settings_.percent) + "%";
    if (updated != display_brightness_label_text_) {
        display_brightness_label_text_ = updated;
        lv_label_set_text(display_brightness_label_, display_brightness_label_text_.c_str());
    }
    if (display_brightness_available_) {
        lv_obj_remove_state(display_brightness_slider_, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(display_brightness_slider_, LV_STATE_DISABLED);
    }
}

void StarterUi::apply_display_brightness_settings() {
    if (!display_brightness_available_ || apply_display_brightness_settings_ == nullptr) {
        return;
    }
    std::string diagnostic;
    if (!apply_display_brightness_settings_(display_brightness_settings_, &diagnostic)) {
        display_brightness_status_text_ = "Unable to apply: " + diagnostic;
        UiTheme::set_role(display_brightness_status_label_, UiThemeRole::ErrorText);
        display_brightness_settings_ = applied_display_brightness_settings_;
        update_display_brightness_controls();
    } else {
        applied_display_brightness_settings_ = display_brightness_settings_;
        display_brightness_status_text_ = "Brightness saved.";
        UiTheme::set_role(display_brightness_status_label_, UiThemeRole::SuccessText);
        update_display_brightness_controls();
    }
    if (display_brightness_status_label_ != nullptr) {
        lv_label_set_text(display_brightness_status_label_, display_brightness_status_text_.c_str());
    }
}

void StarterUi::update_display_standby_controls() {
    if (display_standby_checkbox_ == nullptr || display_standby_slider_ == nullptr ||
        display_standby_label_ == nullptr) {
        return;
    }
    if (display_standby_settings_.enabled) {
        lv_obj_add_state(display_standby_checkbox_, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(display_standby_checkbox_, LV_STATE_CHECKED);
    }
    lv_slider_set_value(display_standby_slider_,
                        static_cast<int>(display_standby_settings_.seconds), LV_ANIM_OFF);
    const std::string updated = "Standby after: " +
                                std::to_string(display_standby_settings_.seconds) + " seconds";
    if (updated != display_standby_label_text_) {
        display_standby_label_text_ = updated;
        lv_label_set_text(display_standby_label_, display_standby_label_text_.c_str());
    }
    if (display_standby_available_ && display_standby_settings_.enabled) {
        lv_obj_remove_state(display_standby_slider_, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(display_standby_slider_, LV_STATE_DISABLED);
    }
    if (!display_standby_available_) {
        lv_obj_add_state(display_standby_checkbox_, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(display_standby_checkbox_, LV_STATE_DISABLED);
    }
    if (display_standby_apply_button_ != nullptr) {
        const bool dirty = display_standby_settings_.enabled != applied_display_standby_settings_.enabled ||
                           display_standby_settings_.seconds != applied_display_standby_settings_.seconds;
        if (display_standby_available_ && dirty) {
            lv_obj_remove_state(display_standby_apply_button_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(display_standby_apply_button_, LV_STATE_DISABLED);
        }
    }
}

void StarterUi::apply_display_standby_settings() {
    if (!display_standby_available_ || apply_display_standby_settings_ == nullptr) {
        return;
    }
    std::string diagnostic;
    if (!apply_display_standby_settings_(display_standby_settings_, &diagnostic)) {
        display_standby_status_text_ = "Unable to save: " + diagnostic;
        UiTheme::set_role(display_standby_status_label_, UiThemeRole::ErrorText);
    } else {
        applied_display_standby_settings_ = display_standby_settings_;
        display_standby_status_text_ = display_standby_settings_.enabled
            ? "Saved. Display will sleep after inactivity."
            : "Saved. Auto standby is off.";
        UiTheme::set_role(display_standby_status_label_, UiThemeRole::SuccessText);
        update_display_standby_controls();
    }
    if (display_standby_status_label_ != nullptr) {
        lv_label_set_text(display_standby_status_label_, display_standby_status_text_.c_str());
    }
}

void StarterUi::update_wifi_password_length() {
    if (!wifi_password_visible_ || wifi_password_input_ == nullptr ||
        wifi_password_length_label_ == nullptr) {
        return;
    }
    // lv_textarea_get_text() exposes the secret, so only inspect its length
    // transiently; do not construct, queue, log, or retain a string from it.
    const auto length = std::strlen(lv_textarea_get_text(wifi_password_input_));
    const std::string updated = "Password length: " + std::to_string(length);
    if (updated != wifi_password_length_text_) {
        wifi_password_length_text_ = updated;
        lv_label_set_text(wifi_password_length_label_, wifi_password_length_text_.c_str());
    }
}

void StarterUi::drain_events() {
    for (auto& event : event_queue_.drain()) {
        if (auto* snapshot = std::get_if<core::NetworkSnapshot>(&event.payload)) {
            network_snapshot_ = std::move(*snapshot);
        } else if (auto* profile = std::get_if<core::ManagedIpv4Profile>(&event.payload)) {
            load_managed_ipv4_profile(*profile);
        } else if (auto* result = std::get_if<core::WifiScanResult>(&event.payload)) {
            wifi_scan_result_ = std::move(*result);
        } else if (auto* update = std::get_if<core::ActionProgressUpdate>(&event.payload)) {
            if (update->job_id == action_runner_job_id_) {
                update_action_runner_progress(update->progress);
            }
        } else if (auto* terminal = std::get_if<core::ActionTerminal>(&event.payload)) {
            if (terminal->job_id == action_runner_job_id_) {
                show_action_runner_result(terminal->result);
            }
        } else if (auto* network_result = std::get_if<core::NetworkApplyResult>(&event.payload)) {
            if (network_result_visible_ && network_apply_pending_ &&
                network_result->request_id == network_apply_request_id_ &&
                network_result_label_ != nullptr) {
                show_network_result(network_result->message, network_result->ok, false);
            }
        } else if (auto* update_result = std::get_if<core::SystemUpdateResult>(&event.payload)) {
            if (system_update_result_visible_ && system_update_pending_ &&
                update_result->request_id == system_update_request_id_ &&
                system_update_result_label_ != nullptr) {
                show_system_update_result(update_result->message, update_result->ok, false);
            }
        } else if (auto* update_progress = std::get_if<core::SystemUpdateProgress>(&event.payload)) {
            if (system_update_result_visible_ && system_update_pending_ &&
                update_progress->request_id == system_update_request_id_ &&
                system_update_result_label_ != nullptr) {
                std::string message;
                if (update_progress->phase == "writing") {
                    message = "Writing inactive slot: " +
                              std::to_string(update_progress->percent) + "%\nDo not remove power.";
                } else if (update_progress->phase == "checking") {
                    message = "Verifying the written root filesystem…";
                } else if (update_progress->phase == "boot-files") {
                    message = "Installing candidate boot files…";
                } else if (update_progress->phase == "arming") {
                    message = "Arming one candidate boot…";
                } else if (update_progress->phase == "failed-source") {
                    message = "No USB stick with a readable FAT32 or exFAT filesystem was found.";
                } else if (update_progress->phase == "failed-compatibility") {
                    message = "This update is for a different panel image or Raspberry Pi board.";
                } else if (update_progress->phase == "failed-payload") {
                    message = "The USB stick must hold exactly one valid .mpupdate file.";
                } else if (update_progress->phase == "failed-version") {
                    message = "This panel already runs that software version.";
                } else if (update_progress->phase == "failed-integrity") {
                    message = "The update payload failed its integrity check.";
                } else if (update_progress->phase == "failed-stall") {
                    message = "The update data stopped arriving.";
                } else if (update_progress->phase == "failed-boot") {
                    message = "The update boot files were refused.";
                } else if (update_progress->phase == "failed-target") {
                    message = "The inactive update slot is unavailable.";
                } else if (update_progress->phase == "failed-selector") {
                    message = "The A/B boot selector is unavailable.";
                } else if (update_progress->phase == "failed-image") {
                    message = "The running system is not prepared for an A/B update.";
                } else if (update_progress->phase == "failed-internal") {
                    message = "The update stopped safely before candidate boot.";
                } else if (update_progress->phase == "scanning") {
                    message = "Looking for an update file on USB media…";
                } else {
                    message = "Validating the update bundle…";
                }
                lv_label_set_text(system_update_result_label_, message.c_str());
            }
        } else if (auto* calibration_sample =
                       std::get_if<core::TouchCalibrationRawSample>(&event.payload)) {
            accept_touch_calibration_sample(*calibration_sample);
        } else if (auto* request = std::get_if<core::UiControlRequest>(&event.payload)) {
            if (request->completion == nullptr) {
                continue;
            }
            if (request->command.type == core::UiControlCommandType::Tap) {
                queue_tap(request->command, std::move(request->completion));
                continue;
            }
            if (request->command.type == core::UiControlCommandType::Text) {
                queue_text(request->command, std::move(request->completion));
                continue;
            }
            try {
                request->completion->set_value(handle_control(request->command));
            } catch (const std::future_error&) {
                // The caller timed out before the UI loop reached it.
            }
        }
    }
    refresh_network_info();
    refresh_wifi_scan();
}

void StarterUi::action_callback(lv_event_t* event) {
    const auto* action = static_cast<const Action*>(lv_event_get_user_data(event));
    action->ui->queue_action(action->id);
}

void StarterUi::ip_input_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    ui->focus_ip_input(static_cast<lv_obj_t*>(lv_event_get_target(event)));
}

void StarterUi::ip_mode_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    ui->ip_settings_profile_loaded_ = true;
    ui->dhcp_server_apply_confirmed_ = false;
    if (lv_dropdown_get_selected(ui->ip_mode_dropdown_) == 1U) {
        // Defaults are a user-selection convenience. Do not use them while
        // rendering a saved profile, where an empty gateway is meaningful and
        // should remain visible for validation rather than being invented.
        ui->set_static_ipv4_defaults();
    } else if (lv_dropdown_get_selected(ui->ip_mode_dropdown_) == 2U) {
        ui->set_dhcp_server_defaults();
    }
    ui->update_ip_settings_mode();
}

void StarterUi::ip_mode_list_draw_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    lv_obj_t* const list = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (ui == nullptr || list == nullptr || lv_obj_get_child_count(list) == 0U) {
        return;
    }

    // A dropdown list is rendered as one multi-line label. Draw each divider
    // ourselves instead of creating a separate, accidentally selectable row.
    lv_obj_t* const label = lv_obj_get_child(list, 0U);
    const lv_font_t* const font = lv_obj_get_style_text_font(list, LV_PART_MAIN);
    if (label == nullptr || font == nullptr) {
        return;
    }
    lv_area_t list_area{};
    lv_area_t label_area{};
    lv_obj_get_coords(list, &list_area);
    lv_obj_get_coords(label, &label_area);
    const int line_height = lv_font_get_line_height(font);
    const int line_space = lv_obj_get_style_text_line_space(list, LV_PART_MAIN);
    lv_draw_line_dsc_t divider{};
    lv_draw_line_dsc_init(&divider);
    divider.base.layer = lv_event_get_layer(event);
    divider.color = UiTheme::to_lv_color(ui->theme_.active_skin().colors.accent);
    divider.opa = LV_OPA_COVER;
    divider.width = 2;
    for (int row = 1; row < 3; ++row) {
        const int divider_y = label_area.y1 + row * (line_height + line_space);
        if (divider_y <= list_area.y1 || divider_y >= list_area.y2) {
            continue;
        }
        divider.p1 = {static_cast<lv_coord_t>(list_area.x1 + 8), divider_y};
        divider.p2 = {static_cast<lv_coord_t>(list_area.x2 - 8), divider_y};
        lv_draw_line(divider.base.layer, &divider);
    }
}

void StarterUi::keyboard_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    if (!ui->ip_settings_visible_ || ui->keyboard_ == nullptr) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CANCEL) {
        ui->dismiss_keyboard();
        return;
    }
    lv_obj_t* const focused = lv_keyboard_get_textarea(ui->keyboard_);
    if (focused == ui->ip_address_input_) {
        ui->focus_ip_input(ui->gateway_input_);
    } else if (focused == ui->gateway_input_) {
        ui->focus_ip_input(ui->netmask_input_);
    } else if (focused == ui->netmask_input_ && ui->ip_mode_dropdown_ != nullptr &&
               lv_dropdown_get_selected(ui->ip_mode_dropdown_) == 2U) {
        ui->focus_ip_input(ui->lease_end_input_);
    } else {
        ui->validate_ip_settings();
    }
}

void StarterUi::wifi_password_input_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    if (!ui->wifi_password_visible_ || ui->keyboard_ == nullptr) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        lv_obj_remove_flag(ui->keyboard_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(ui->wifi_password_input_, LV_STATE_FOCUSED);
        lv_obj_send_event(ui->wifi_password_input_, LV_EVENT_FOCUSED, nullptr);
        lv_keyboard_set_textarea(ui->keyboard_, ui->wifi_password_input_);
        return;
    }
    ui->update_wifi_password_length();
}

void StarterUi::wifi_password_keyboard_navigation_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    if (!ui->wifi_password_visible_) {
        return;
    }
    auto* const keyboard = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    const std::uint32_t button = lv_buttonmatrix_get_selected_button(keyboard);
    if (button == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    const char* const text = lv_buttonmatrix_get_button_text(keyboard, button);
    if (text == nullptr) {
        return;
    }

    const lv_keyboard_mode_t current = lv_keyboard_get_mode(keyboard);
    lv_keyboard_mode_t target = current;
    if (std::strcmp(text, "Next") == 0) {
        if (current == LV_KEYBOARD_MODE_USER_1) {
            target = LV_KEYBOARD_MODE_USER_2;
        } else if (current == LV_KEYBOARD_MODE_USER_3) {
            target = LV_KEYBOARD_MODE_USER_4;
        } else {
            target = LV_KEYBOARD_MODE_SPECIAL;
        }
    } else if (std::strcmp(text, "Prev") == 0) {
        if (current == LV_KEYBOARD_MODE_SPECIAL) {
            target = ui->wifi_password_uppercase_ ? LV_KEYBOARD_MODE_USER_4
                                                   : LV_KEYBOARD_MODE_USER_2;
        } else {
            target = current == LV_KEYBOARD_MODE_USER_2 ? LV_KEYBOARD_MODE_USER_1
                                                         : LV_KEYBOARD_MODE_USER_3;
        }
    } else if (std::strcmp(text, "ABC") == 0) {
        target = current == LV_KEYBOARD_MODE_USER_2 ? LV_KEYBOARD_MODE_USER_4
                                                     : LV_KEYBOARD_MODE_USER_3;
        ui->wifi_password_uppercase_ = true;
    } else if (std::strcmp(text, "abc") == 0) {
        target = current == LV_KEYBOARD_MODE_USER_4 ? LV_KEYBOARD_MODE_USER_2
                                                     : LV_KEYBOARD_MODE_USER_1;
        ui->wifi_password_uppercase_ = false;
    } else {
        return;
    }
    lv_keyboard_set_mode(keyboard, target);
    lv_event_stop_processing(event);
}

void StarterUi::wifi_password_keyboard_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    if (!ui->wifi_password_visible_ || ui->keyboard_ == nullptr) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CANCEL) {
        ui->show_parent_menu();
        return;
    }

    ui->update_wifi_password_length();
    if (ui->wifi_password_visibility_control_ != nullptr &&
        ui->wifi_password_visibility_control_->button != nullptr) {
        lv_obj_remove_state(ui->wifi_password_visibility_control_->button, LV_STATE_CHECKED);
        update_password_visibility_control(ui->wifi_password_visibility_control_.get());
    }
    lv_obj_add_flag(ui->keyboard_, LV_OBJ_FLAG_HIDDEN);
    if (ui->wifi_password_length_label_ != nullptr) {
        lv_obj_add_flag(ui->wifi_password_length_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui->wifi_password_status_label_ != nullptr) {
        lv_label_set_text(ui->wifi_password_status_label_, "Mock submit - no connection made");
        UiTheme::set_role(ui->wifi_password_status_label_, UiThemeRole::SuccessText);
        lv_obj_remove_flag(ui->wifi_password_status_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StarterUi::drain_timer_callback(lv_timer_t* timer) {
    auto* ui = static_cast<StarterUi*>(lv_timer_get_user_data(timer));
    ui->drain_events();
}

void StarterUi::progress_timer_callback(lv_timer_t* timer) {
    auto* ui = static_cast<StarterUi*>(lv_timer_get_user_data(timer));
    ui->update_progress_demo();
}

void StarterUi::action_progress_timer_callback(lv_timer_t* timer) {
    auto* ui = static_cast<StarterUi*>(lv_timer_get_user_data(timer));
    if (ui->action_runner_visible_ && ui->action_runner_running_ && ui->refresh_action_progress_) {
        ui->refresh_action_progress_(ui->action_runner_job_id_);
    }
}

void StarterUi::slider_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    ui->update_slider_demo();
}

void StarterUi::display_brightness_slider_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    if (ui->display_brightness_slider_ == nullptr) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        const platform::DisplayBrightnessSettings requested{static_cast<unsigned int>(std::clamp(
            lv_slider_get_value(ui->display_brightness_slider_),
            static_cast<int>(platform::kDisplayBrightnessMinimumPercent),
            static_cast<int>(platform::kDisplayBrightnessMaximumPercent)))};
        std::string diagnostic;
        if (ui->preview_display_brightness_ == nullptr ||
            !ui->preview_display_brightness_(requested, &diagnostic)) {
            ui->display_brightness_settings_ = ui->applied_display_brightness_settings_;
            ui->display_brightness_status_text_ = "Unable to change brightness: " + diagnostic;
            if (ui->display_brightness_status_label_ != nullptr) {
                lv_label_set_text(ui->display_brightness_status_label_,
                                  ui->display_brightness_status_text_.c_str());
                UiTheme::set_role(ui->display_brightness_status_label_, UiThemeRole::ErrorText);
            }
            ui->update_display_brightness_controls();
            return;
        }
        ui->display_brightness_settings_ = requested;
        ui->update_display_brightness_controls();
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_RELEASED &&
        ui->display_brightness_settings_.percent !=
            ui->applied_display_brightness_settings_.percent) {
        ui->apply_display_brightness_settings();
    }
}

void StarterUi::display_standby_checkbox_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    ui->display_standby_settings_.enabled =
        lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_STATE_CHECKED);
    ui->update_display_standby_controls();
}

void StarterUi::display_standby_slider_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    if (ui->display_standby_slider_ == nullptr) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        const int value = lv_slider_get_value(ui->display_standby_slider_);
        const int step = static_cast<int>(platform::kDisplayStandbyStepSeconds);
        const int snapped = std::clamp(
            ((value + step / 2) / step) * step,
            static_cast<int>(platform::kDisplayStandbyMinimumSeconds),
            static_cast<int>(platform::kDisplayStandbyMaximumSeconds));
        ui->display_standby_settings_.seconds = static_cast<unsigned int>(snapped);
        if (value != snapped) {
            lv_slider_set_value(ui->display_standby_slider_, snapped, LV_ANIM_OFF);
        }
        ui->update_display_standby_controls();
    }
}

void StarterUi::screen_lock_input_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    if (ui == nullptr) {
        return;
    }
    ui->focus_screen_lock_input(static_cast<lv_obj_t*>(lv_event_get_current_target(event)));
}

void StarterUi::update_password_visibility_control(PasswordVisibilityControl* control) {
    if (control == nullptr || control->input == nullptr || control->button == nullptr ||
        control->icon == nullptr) {
        return;
    }
    const bool show = lv_obj_has_state(control->button, LV_STATE_CHECKED);
    lv_textarea_set_password_mode(control->input, !show);
    lv_label_set_text(control->icon, show ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    if (!show) {
        // The default delays masking the last character. These secret fields
        // require an explicit eye tap for every reveal.
        lv_textarea_set_password_show_time(control->input, 0U);
    }
}

void StarterUi::password_visibility_callback(lv_event_t* event) {
    update_password_visibility_control(
        static_cast<PasswordVisibilityControl*>(lv_event_get_user_data(event)));
}

void StarterUi::screen_lock_keyboard_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    if (ui == nullptr || ui->screen_lock_keyboard_ == nullptr) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CANCEL) {
        // A locked panel may never expose a Back route. Configuration forms
        // can safely return to their dedicated settings page.
        if (ui->screen_lock_visible_) {
            ui->focus_screen_lock_input(ui->screen_lock_pin_input_);
        } else {
            ui->show_screen_lock_settings();
        }
        return;
    }
    if (ui->screen_lock_pin_setup_visible_) {
        if (lv_keyboard_get_textarea(ui->screen_lock_keyboard_) == ui->screen_lock_pin_input_) {
            ui->focus_screen_lock_input(ui->screen_lock_pin_confirm_input_);
        } else {
            ui->submit_screen_lock_pin_setup();
        }
    } else if (ui->screen_lock_disable_visible_) {
        ui->submit_screen_lock_disable();
    } else if (ui->screen_lock_visible_) {
        ui->submit_screen_lock_unlock();
    }
}

void StarterUi::deferred_action_callback(void* user_data) {
    auto* const pending = static_cast<PendingAction*>(user_data);
    StarterUi* const ui = pending->ui;
    const auto found = std::find_if(ui->pending_actions_.begin(), ui->pending_actions_.end(),
                                    [pending](const auto& candidate) {
                                        return candidate.get() == pending;
                                    });
    if (found == ui->pending_actions_.end()) {
        return;
    }
    const std::string id = (*found)->id;
    ui->pending_actions_.erase(found);
    ui->activate(id);
}

void StarterUi::deferred_tap_reply_timer_callback(lv_timer_t* timer) {
    auto* const pending = static_cast<PendingTapReply*>(lv_timer_get_user_data(timer));
    StarterUi* const ui = pending->ui;
    const auto found = std::find_if(ui->pending_tap_replies_.begin(), ui->pending_tap_replies_.end(),
                                    [pending](const auto& candidate) {
                                        return candidate.get() == pending;
                                    });
    if (found == ui->pending_tap_replies_.end()) {
        return;
    }
    const auto completion = (*found)->completion;
    ui->pending_tap_replies_.erase(found);
    ui->settle_render();
    if (completion != nullptr) {
        try {
            completion->set_value(ui->state_response());
        } catch (const std::future_error&) {
            // The control peer already disconnected.
        }
    }
}

}  // namespace micropanel_touch::ui
