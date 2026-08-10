#include "ui/StarterUi.h"

#include <sstream>
#include <utility>

namespace micropanel_touch::ui {
namespace {

constexpr int kHorizontalMargin = 16;

}  // namespace

StarterUi::StarterUi(StarterConfig config, core::UiEventQueue& event_queue)
    : config_(std::move(config)), event_queue_(event_queue) {}

StarterUi::~StarterUi() {
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
    // This is the usable Sprint 1 baseline, not the configurable theme system
    // planned for Sprint 3.
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xe8edf2), 0);
    actions_.clear();
    network_label_ = nullptr;
    network_info_visible_ = false;
    network_text_.clear();
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

int StarterUi::first_button_y() const {
    return screen_height() > screen_width() ? 60 : 56;
}

int StarterUi::button_spacing() const {
    return button_height() + 8;
}

void StarterUi::create_title(const std::string& title) {
    lv_obj_t* const label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, title.c_str());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
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
    lv_obj_set_style_bg_color(button, lv_color_hex(0x263746), 0);

    auto callback = std::make_unique<Action>(Action{this, action});
    lv_obj_add_event_cb(button, action_callback, LV_EVENT_CLICKED, callback.get());
    actions_.push_back(std::move(callback));

    lv_obj_t* const label = lv_label_create(button);
    lv_label_set_text(label, title.c_str());
    lv_obj_center(label);
}

void StarterUi::show_root() {
    clear_screen();
    current_menu_id_.clear();
    create_title("MicroPanel Touch");
    int y = first_button_y();
    for (const StarterModule* module : config_.root_menus()) {
        create_button(module->title, y, module->id);
        y += button_spacing();
    }
}

void StarterUi::show_menu(const StarterModule& menu) {
    clear_screen();
    current_menu_id_ = menu.id;
    create_title(menu.title);
    int y = first_button_y();
    for (const auto& item : menu.submenus) {
        create_button(item.title, y, item.id == "back" ? "__root" : item.id);
        y += button_spacing();
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
    lv_obj_set_style_text_color(network_label_, lv_color_hex(0xd7e0e8), 0);
    create_button("Back", screen_height() - button_height() - 12,
                  current_menu_id_.empty() ? "__root" : current_menu_id_);
    refresh_network_info();
}

void StarterUi::create_ip_input(const char* placeholder, int y, const char* accepted_characters,
                                lv_obj_t** input) {
    *input = lv_textarea_create(lv_screen_active());
    lv_obj_set_size(*input, screen_width() - 2 * kHorizontalMargin, 36);
    lv_obj_align(*input, LV_ALIGN_TOP_MID, 0, y);
    lv_textarea_set_one_line(*input, true);
    lv_textarea_set_placeholder_text(*input, placeholder);
    lv_textarea_set_accepted_chars(*input, accepted_characters);
    lv_obj_set_style_bg_color(*input, lv_color_hex(0x1d2a34), 0);
    lv_obj_set_style_border_color(*input, lv_color_hex(0x55748a), 0);
    lv_obj_set_style_text_color(*input, lv_color_hex(0xe8edf2), 0);
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
    lv_obj_set_style_text_color(ip_status_label_, lv_color_hex(0xd7e0e8), 0);

    const bool portrait = screen_height() > screen_width();
    const int input_y = portrait ? 62 : 56;
    const int input_spacing = portrait ? 44 : 34;
    create_ip_input("IP address", input_y, "0123456789.", &ip_address_input_);
    create_ip_input("Prefix length (0-32)", input_y + input_spacing, "0123456789", &prefix_input_);
    create_ip_input("Gateway", input_y + 2 * input_spacing, "0123456789.", &gateway_input_);

    const int keyboard_y = portrait ? 302 : 190;
    if (portrait) {
        create_button("Validate inputs", 194, "__validate_ip");
        create_button("Back", 246, current_menu_id_.empty() ? "__root" : current_menu_id_);
    } else {
        const int gap = 8;
        const int width = (screen_width() - 2 * kHorizontalMargin - gap) / 2;
        create_button("Validate", kHorizontalMargin, 150, width, 34, "__validate_ip");
        create_button("Back", kHorizontalMargin + width + gap, 150, width, 34,
                      current_menu_id_.empty() ? "__root" : current_menu_id_);
    }

    keyboard_ = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(keyboard_, ip_address_input_);
    lv_obj_set_size(keyboard_, screen_width(), screen_height() - keyboard_y);
    lv_obj_align(keyboard_, LV_ALIGN_TOP_MID, 0, keyboard_y);
    lv_obj_add_event_cb(keyboard_, keyboard_callback, LV_EVENT_READY, this);
    lv_obj_add_state(ip_address_input_, LV_STATE_FOCUSED);
}

void StarterUi::show_placeholder(const std::string& title) {
    clear_screen();
    create_title(title);
    lv_obj_t* const label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Coming in this Sprint 1 vertical slice");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -16);
    create_button("Back", screen_height() - button_height() - 12,
                  current_menu_id_.empty() ? "__root" : current_menu_id_);
}

void StarterUi::activate(const std::string& id) {
    if (id == "__root") {
        show_root();
        return;
    }
    if (id == "netinfo") {
        show_network_info();
        return;
    }
    if (id == "netsettings") {
        show_ip_settings();
        return;
    }
    if (id == "__validate_ip") {
        validate_ip_settings();
        return;
    }
    if (const StarterModule* module = config_.find(id); module != nullptr && module->type == "menu") {
        show_menu(*module);
        return;
    }
    if (const StarterModule* module = config_.find(id); module != nullptr) {
        show_placeholder(module->title);
        return;
    }
    show_placeholder(id);
}

void StarterUi::focus_ip_input(lv_obj_t* input) {
    if (!ip_settings_visible_ || keyboard_ == nullptr || input == nullptr) {
        return;
    }
    lv_keyboard_set_textarea(keyboard_, input);
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
    lv_obj_set_style_text_color(ip_status_label_,
                                lv_color_hex(result.valid ? 0x8ee5a1 : 0xffa6a6), 0);
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

void StarterUi::drain_events() {
    for (auto& event : event_queue_.drain()) {
        if (const auto* snapshot = std::get_if<core::NetworkSnapshot>(&event.payload)) {
            network_snapshot_ = std::move(*snapshot);
        }
    }
    refresh_network_info();
}

void StarterUi::action_callback(lv_event_t* event) {
    const auto* action = static_cast<const Action*>(lv_event_get_user_data(event));
    const std::string id = action->id;
    action->ui->activate(id);
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

}  // namespace micropanel_touch::ui
