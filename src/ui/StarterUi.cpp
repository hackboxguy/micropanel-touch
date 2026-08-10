#include "ui/StarterUi.h"

#include "ui/BuiltinIcon.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <utility>

namespace micropanel_touch::ui {
namespace {

constexpr int kHorizontalMargin = 16;
constexpr int kMenuBottomMargin = 12;
constexpr int kMenuGap = 8;
constexpr auto kProgressDemoDuration = std::chrono::seconds(30);
constexpr std::uint32_t kProgressDemoPeriodMs = 200U;
constexpr int kSliderTrackThickness = 8;
constexpr int kSliderHitThickness = 40;
constexpr int kSliderHitPadding = (kSliderHitThickness - kSliderTrackThickness) / 2;

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
    if (progress_timer_ != nullptr) {
        lv_timer_delete(progress_timer_);
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
    if (progress_timer_ != nullptr) {
        lv_timer_delete(progress_timer_);
        progress_timer_ = nullptr;
    }
    // The starter UI never copies a Wi-Fi password outside LVGL. Clear the
    // widget before its screen is torn down so it cannot survive a navigation
    // event in a still-renderable text area.
    if (wifi_password_input_ != nullptr) {
        lv_textarea_set_text(wifi_password_input_, "");
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
    brightness_slider_ = nullptr;
    volume_slider_ = nullptr;
    brightness_slider_label_ = nullptr;
    volume_slider_label_ = nullptr;
    slider_brightness_text_.clear();
    slider_volume_text_.clear();
    wifi_password_visible_ = false;
    wifi_password_uppercase_ = false;
    wifi_password_input_ = nullptr;
    wifi_password_length_label_ = nullptr;
    wifi_password_status_label_ = nullptr;
    wifi_password_visibility_button_ = nullptr;
    wifi_password_visibility_icon_ = nullptr;
    wifi_password_length_text_.clear();
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
    std::string button_text;
    if (action == "__back") {
        button_text = builtin_icon_symbol("back");
        button_text += "  ";
    }
    button_text += title;
    lv_label_set_text(label, button_text.c_str());
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
    lv_textarea_set_cursor_click_pos(*input, true);
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
    focus_ip_input(ip_address_input_);
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

void StarterUi::show_wifi_password_demo() {
    clear_screen();
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

    wifi_password_visibility_button_ = lv_button_create(lv_screen_active());
    lv_obj_set_size(wifi_password_visibility_button_, 44, input_height - 4);
    lv_obj_set_pos(wifi_password_visibility_button_,
                   screen_width() - kHorizontalMargin - 46, input_y + 2);
    lv_obj_add_flag(wifi_password_visibility_button_, LV_OBJ_FLAG_CHECKABLE);
    // It should look like an icon inside the edit field, while retaining a
    // 40+ px touch target and clear pressed feedback.
    lv_obj_set_style_bg_opa(wifi_password_visibility_button_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_password_visibility_button_, 0, 0);
    lv_obj_set_style_bg_color(wifi_password_visibility_button_,
                              UiTheme::to_lv_color(theme_.active_skin().colors.chrome),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(wifi_password_visibility_button_, LV_OPA_30,
                            LV_STATE_PRESSED);
    lv_obj_add_event_cb(wifi_password_visibility_button_, wifi_password_visibility_callback,
                        LV_EVENT_VALUE_CHANGED, this);
    wifi_password_visibility_icon_ = lv_label_create(wifi_password_visibility_button_);
    lv_label_set_text(wifi_password_visibility_icon_, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(wifi_password_visibility_icon_,
                                UiTheme::to_lv_color(theme_.active_skin().colors.accent), 0);
    lv_obj_center(wifi_password_visibility_icon_);

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

void StarterUi::show_slider_demo() {
    clear_screen();
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
    if (id == "slider_demo") {
        navigation_.enter_leaf();
        show_slider_demo();
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
    if (ip_address_input_ != nullptr) {
        lv_obj_remove_state(ip_address_input_, LV_STATE_FOCUSED);
    }
    if (prefix_input_ != nullptr) {
        lv_obj_remove_state(prefix_input_, LV_STATE_FOCUSED);
    }
    if (gateway_input_ != nullptr) {
        lv_obj_remove_state(gateway_input_, LV_STATE_FOCUSED);
    }
    lv_obj_add_state(input, LV_STATE_FOCUSED);
    // Adding the visual focused state alone does not emit LV_EVENT_FOCUSED,
    // which is what starts LVGL's cursor-blink animation.
    lv_obj_send_event(input, LV_EVENT_FOCUSED, nullptr);
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

void StarterUi::update_wifi_password_visibility() {
    if (!wifi_password_visible_ || wifi_password_input_ == nullptr ||
        wifi_password_visibility_button_ == nullptr || wifi_password_visibility_icon_ == nullptr) {
        return;
    }
    const bool show = lv_obj_has_state(wifi_password_visibility_button_, LV_STATE_CHECKED);
    lv_textarea_set_password_mode(wifi_password_input_, !show);
    lv_label_set_text(wifi_password_visibility_icon_,
                      show ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    if (!show) {
        // The default delays masking the last character. This screen must not.
        lv_textarea_set_password_show_time(wifi_password_input_, 0);
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

void StarterUi::wifi_password_visibility_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    ui->update_wifi_password_visibility();
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
    if (ui->wifi_password_visibility_button_ != nullptr) {
        lv_obj_remove_state(ui->wifi_password_visibility_button_, LV_STATE_CHECKED);
        ui->update_wifi_password_visibility();
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

void StarterUi::slider_callback(lv_event_t* event) {
    auto* ui = static_cast<StarterUi*>(lv_event_get_user_data(event));
    ui->update_slider_demo();
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
