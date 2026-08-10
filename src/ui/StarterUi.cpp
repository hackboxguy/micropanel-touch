#include "ui/StarterUi.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace micropanel_touch::ui {
namespace {

constexpr int kHorizontalMargin = 16;
constexpr int kMenuBottomMargin = 12;
constexpr int kMenuGap = 8;

}  // namespace

StarterUi::StarterUi(StarterConfig config, const UiTheme& theme, core::UiEventQueue& event_queue,
                     std::function<void()> request_wifi_scan,
                     std::function<bool(const std::string&, std::string*)> select_theme,
                     std::function<std::string()> active_theme_name)
    : config_(std::move(config)), theme_(theme), event_queue_(event_queue),
      request_wifi_scan_(std::move(request_wifi_scan)), select_theme_(std::move(select_theme)),
      active_theme_name_(std::move(active_theme_name)) {}

StarterUi::~StarterUi() {
    for (const auto& action : pending_actions_) {
        lv_async_call_cancel(deferred_action_callback, action.get());
    }
    if (event_timer_ != nullptr) {
        lv_timer_delete(event_timer_);
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

void StarterUi::clear_screen() {
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
    ip_settings_visible_ = false;
    ip_address_input_ = nullptr;
    prefix_input_ = nullptr;
    gateway_input_ = nullptr;
    ip_status_label_ = nullptr;
    keyboard_ = nullptr;
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

void StarterUi::create_title(const std::string& title) {
    lv_obj_t* const label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, title.c_str());
    UiTheme::set_role(label, UiThemeRole::Title);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 14);
}

void StarterUi::create_button(const std::string& title, int y, const std::string& action) {
    const int width = screen_width() - 2 * kHorizontalMargin;
    create_button(title, (screen_width() - width) / 2, y, width, button_height(), action);
}

void StarterUi::create_button(const std::string& title, int x, int y, int width, int height,
                              const std::string& action) {
    lv_obj_t* const button = lv_button_create(lv_screen_active());
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);

    auto callback = std::make_unique<Action>(Action{this, action});
    lv_obj_add_event_cb(button, action_callback, LV_EVENT_CLICKED, callback.get());
    actions_.push_back(std::move(callback));

    lv_obj_t* const label = lv_label_create(button);
    lv_label_set_text(label, title.c_str());
    lv_obj_center(label);
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

void StarterUi::create_menu_button(const std::string& title, const std::string& color,
                                   const std::string& action,
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
    const int width = grid
                          ? (content_width - static_cast<int>(columns - 1U) * kMenuGap) /
                                static_cast<int>(columns)
                          : content_width;
    const int tile_height = std::max(button_height(),
                                     std::min(width, (content_height - kMenuGap) / 2));

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

    lv_obj_t* const label = lv_label_create(button);
    lv_label_set_text(label, title.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void StarterUi::show_root() {
    clear_screen();
    navigation_.reset();
    create_title("MicroPanel Touch");
    const StarterMenuPresentation& presentation = config_.root_presentation();
    create_menu_content(presentation);
    for (const StarterModule* module : config_.root_menus()) {
        create_menu_button(module->title, module->presentation.accent, module->id, presentation);
    }
}

void StarterUi::show_menu(const StarterModule& menu) {
    clear_screen();
    create_title(menu.title);
    create_menu_content(menu.presentation);
    for (const auto& item : menu.submenus) {
        create_menu_button(item.title, item.color.empty() ? menu.presentation.accent : item.color,
                           item.id == "back" ? "__back" : item.id, menu.presentation);
    }
}

void StarterUi::show_network_info() {
    clear_screen();
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
    lv_obj_add_event_cb(*input, ip_input_callback, LV_EVENT_CLICKED, this);
}

void StarterUi::show_ip_settings() {
    clear_screen();
    ip_settings_visible_ = true;
    create_title("IP Settings");

    ip_status_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(ip_status_label_, screen_width() - 2 * kHorizontalMargin);
    lv_label_set_long_mode(ip_status_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ip_status_label_, "Local validation only; no network changes.");
    lv_obj_align(ip_status_label_, LV_ALIGN_TOP_MID, 0, 36);
    UiTheme::set_role(ip_status_label_, UiThemeRole::DimText);

    const bool portrait = screen_height() > screen_width();
    const int input_y = portrait ? 58 : 56;
    const int input_spacing = portrait ? 46 : 34;
    const int input_height = portrait ? 44 : 30;
    create_ip_input("IP address", input_y, input_height, "0123456789.", &ip_address_input_);
    create_ip_input("Prefix length (0-32)", input_y + input_spacing, input_height,
                    "0123456789", &prefix_input_);
    create_ip_input("Gateway", input_y + 2 * input_spacing, input_height, "0123456789.",
                    &gateway_input_);

    const int keyboard_y = portrait ? 306 : 190;
    if (portrait) {
        create_button("Validate inputs", 200, "__validate_ip");
        create_button("Back", 252, "__back");
    } else {
        const int gap = 8;
        const int width = (screen_width() - 2 * kHorizontalMargin - gap) / 2;
        create_button("Validate", kHorizontalMargin, 150, width, 34, "__validate_ip");
        create_button("Back", kHorizontalMargin + width + gap, 150, width, 34,
                      "__back");
    }

    keyboard_ = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(keyboard_, ip_address_input_);
    lv_obj_set_size(keyboard_, screen_width(), screen_height() - keyboard_y);
    lv_obj_align(keyboard_, LV_ALIGN_TOP_MID, 0, keyboard_y);
    lv_obj_add_event_cb(keyboard_, keyboard_callback, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_callback, LV_EVENT_CANCEL, this);
    lv_obj_add_state(ip_address_input_, LV_STATE_FOCUSED);
}

void StarterUi::show_wifi() {
    clear_screen();
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

void StarterUi::show_theme_selection() {
    clear_screen();
    create_title("Theme");

    lv_obj_t* const status = lv_label_create(lv_screen_active());
    const std::string active_name = active_theme_name_ ? active_theme_name_() : "unknown";
    const std::string text = theme_message_.empty() ? "Active: " + active_name : theme_message_;
    lv_label_set_text(status, text.c_str());
    UiTheme::set_role(status, theme_message_.empty() ? UiThemeRole::DimText : UiThemeRole::ErrorText);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 44);

    StarterMenuPresentation presentation;
    create_menu_content(presentation, 76);
    create_menu_button("Dark", "", "__theme:dark", presentation);
    create_menu_button("Light", "", "__theme:light", presentation);
    create_menu_button("High contrast", "", "__theme:high-contrast", presentation);
    create_menu_button("Back", "", "__back", presentation);
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
    if (id == "__root") {
        show_root();
        return;
    }
    if (id == "__back") {
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
    if (id == "wifi") {
        navigation_.enter_leaf();
        show_wifi();
        return;
    }
    if (id == "theme_select") {
        navigation_.enter_leaf();
        theme_message_.clear();
        show_theme_selection();
        return;
    }
    if (id == "__validate_ip") {
        validate_ip_settings();
        return;
    }
    if (id == "__wifi_scan") {
        request_wifi_scan();
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
        show_placeholder(module->title);
        return;
    }
    navigation_.enter_leaf();
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

void StarterUi::focus_ip_input(lv_obj_t* input) {
    if (!ip_settings_visible_ || keyboard_ == nullptr || input == nullptr) {
        return;
    }
    lv_obj_remove_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(keyboard_, input);
}

void StarterUi::dismiss_keyboard() {
    if (keyboard_ == nullptr) {
        return;
    }
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    if (ip_address_input_ != nullptr) {
        lv_obj_remove_state(ip_address_input_, LV_STATE_FOCUSED);
    }
    if (prefix_input_ != nullptr) {
        lv_obj_remove_state(prefix_input_, LV_STATE_FOCUSED);
    }
    if (gateway_input_ != nullptr) {
        lv_obj_remove_state(gateway_input_, LV_STATE_FOCUSED);
    }
}

void StarterUi::validate_ip_settings() {
    if (!ip_settings_visible_ || ip_status_label_ == nullptr || ip_address_input_ == nullptr ||
        prefix_input_ == nullptr || gateway_input_ == nullptr) {
        return;
    }
    const core::StaticIpSettings settings{
        lv_textarea_get_text(ip_address_input_),
        lv_textarea_get_text(prefix_input_),
        lv_textarea_get_text(gateway_input_),
    };
    const core::StaticIpValidationResult result = core::validate_static_ipv4(settings);
    lv_label_set_text(ip_status_label_, result.message.c_str());
    UiTheme::set_role(ip_status_label_,
                      result.valid ? UiThemeRole::SuccessText : UiThemeRole::ErrorText);
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

void StarterUi::drain_events() {
    for (auto& event : event_queue_.drain()) {
        if (auto* snapshot = std::get_if<core::NetworkSnapshot>(&event.payload)) {
            network_snapshot_ = std::move(*snapshot);
        } else if (auto* result = std::get_if<core::WifiScanResult>(&event.payload)) {
            wifi_scan_result_ = std::move(*result);
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
        ui->focus_ip_input(ui->prefix_input_);
    } else if (focused == ui->prefix_input_) {
        ui->focus_ip_input(ui->gateway_input_);
    } else {
        ui->validate_ip_settings();
    }
}

void StarterUi::drain_timer_callback(lv_timer_t* timer) {
    auto* ui = static_cast<StarterUi*>(lv_timer_get_user_data(timer));
    ui->drain_events();
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

}  // namespace micropanel_touch::ui
