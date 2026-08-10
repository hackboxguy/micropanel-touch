#include "ui/StarterUi.h"

#include <sstream>
#include <utility>

namespace micropanel_touch::ui {
namespace {

constexpr int kScreenWidth = 480;
constexpr int kButtonWidth = 440;
constexpr int kButtonHeight = 48;
constexpr int kFirstButtonY = 56;
constexpr int kButtonSpacing = 54;

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
}

void StarterUi::create_title(const std::string& title) {
    lv_obj_t* const label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, title.c_str());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 14);
}

void StarterUi::create_button(const std::string& title, int y, const std::string& action) {
    lv_obj_t* const button = lv_button_create(lv_screen_active());
    lv_obj_set_size(button, kButtonWidth, kButtonHeight);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
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
    int y = kFirstButtonY;
    for (const StarterModule* module : config_.root_menus()) {
        create_button(module->title, y, module->id);
        y += kButtonSpacing;
    }
}

void StarterUi::show_menu(const StarterModule& menu) {
    clear_screen();
    current_menu_id_ = menu.id;
    create_title(menu.title);
    int y = kFirstButtonY;
    for (const auto& item : menu.submenus) {
        create_button(item.title, y, item.id == "back" ? "__root" : item.id);
        y += kButtonSpacing;
    }
}

void StarterUi::show_network_info() {
    clear_screen();
    network_info_visible_ = true;
    create_title("Network Info");

    network_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(network_label_, kScreenWidth - 32);
    lv_label_set_long_mode(network_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(network_label_, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_color(network_label_, lv_color_hex(0xd7e0e8), 0);
    create_button("Back", 260, current_menu_id_.empty() ? "__root" : current_menu_id_);
    refresh_network_info();
}

void StarterUi::show_placeholder(const std::string& title) {
    clear_screen();
    create_title(title);
    lv_obj_t* const label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Coming in this Sprint 1 vertical slice");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -16);
    create_button("Back", 260, current_menu_id_.empty() ? "__root" : current_menu_id_);
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
    action->ui->activate(action->id);
}

void StarterUi::drain_timer_callback(lv_timer_t* timer) {
    auto* ui = static_cast<StarterUi*>(lv_timer_get_user_data(timer));
    ui->drain_events();
}

}  // namespace micropanel_touch::ui
