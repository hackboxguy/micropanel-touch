#include "ui/LegacyUi.h"

#include "ui/BuiltinIcon.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace micropanel_touch::ui {
namespace {

constexpr int kHorizontalMargin = 16;
constexpr int kMenuBottomMargin = 12;
constexpr int kMenuGap = 8;

const char* module_type_name(core::LegacyModuleType type) {
    switch (type) {
    case core::LegacyModuleType::Builtin:
        return "Built-in module";
    case core::LegacyModuleType::Menu:
        return "Menu";
    case core::LegacyModuleType::GenericList:
        return "Generic list";
    case core::LegacyModuleType::Textbox:
        return "Textbox module";
    case core::LegacyModuleType::Action:
        return "Action module";
    }
    return "Unknown module";
}

}  // namespace

LegacyUi::LegacyUi(const core::LegacyConfig& config) : config_(config) {}

LegacyUi::~LegacyUi() {
    for (const auto& action : pending_actions_) {
        lv_async_call_cancel(deferred_action_callback, action.get());
    }
}

void LegacyUi::start() {
    show_root();
    // The framebuffer driver can learn its actual geometry after the root
    // screen exists. Force the initial real-config menu to lay out now, so it
    // is as sharp and complete as the starter UI before the first touch.
    lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());
}

void LegacyUi::clear_screen() {
    lv_obj_clean(lv_screen_active());
    actions_.clear();
    menu_content_ = nullptr;
}

void LegacyUi::create_title(const std::string& title) {
    lv_obj_t* const label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, title.c_str());
    UiTheme::set_role(label, UiThemeRole::Title);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 14);
}

void LegacyUi::create_menu_content(int top) {
    menu_content_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(menu_content_, screen_width() - 2 * kHorizontalMargin,
                    screen_height() - top - kMenuBottomMargin);
    lv_obj_align(menu_content_, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_style_bg_opa(menu_content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_content_, 0, 0);
    lv_obj_set_style_pad_all(menu_content_, 0, 0);
    lv_obj_set_style_pad_row(menu_content_, kMenuGap, 0);
    lv_obj_set_scrollbar_mode(menu_content_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(menu_content_, LV_DIR_VER);
    lv_obj_set_flex_flow(menu_content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
}

void LegacyUi::create_menu_button(const std::string& title, ActionKind kind,
                                  const std::string& module_id, std::size_t list_index) {
    if (menu_content_ == nullptr) {
        return;
    }

    lv_obj_t* const button = lv_button_create(menu_content_);
    lv_obj_set_size(button, screen_width() - 2 * kHorizontalMargin, button_height());

    auto action = std::make_unique<Action>(Action{this, kind, module_id, list_index});
    lv_obj_add_event_cb(button, action_callback, LV_EVENT_CLICKED, action.get());
    actions_.push_back(std::move(action));

    std::string button_text;
    if (kind == ActionKind::Back) {
        button_text = builtin_icon_symbol("back");
        button_text += "  ";
    }
    button_text += title;
    lv_obj_t* const label = lv_label_create(button);
    lv_label_set_text(label, button_text.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void LegacyUi::show_root() {
    clear_screen();
    navigation_.reset();
    pending_list_id_.clear();
    create_title("MicroPanel Touch");
    create_menu_content();
    for (const core::LegacyModule* module : config_.root_modules()) {
        create_menu_button(module->title, ActionKind::Module, module->id);
    }
}

void LegacyUi::show_menu(const core::LegacyModule& menu) {
    clear_screen();
    pending_list_id_.clear();
    create_title(menu.title);
    create_menu_content();
    for (const auto& item : menu.submenus) {
        create_menu_button(item.title, item.is_back() ? ActionKind::Back : ActionKind::Module,
                           item.id);
    }
}

void LegacyUi::show_generic_list(const core::LegacyModule& list) {
    clear_screen();
    pending_list_id_.clear();
    create_title(list.title);
    create_menu_content();
    for (std::size_t index = 0; index < list.list_items.size(); ++index) {
        const core::LegacyListItem& item = list.list_items[index];
        create_menu_button(item.title, item.is_back() ? ActionKind::Back : ActionKind::ListItem,
                           list.id, index);
    }
    if (list.has_dynamic_items()) {
        create_menu_button("Dynamic items pending", ActionKind::ListItem, list.id,
                           list.list_items.size());
    }
}

void LegacyUi::show_not_implemented(const core::LegacyModule& module) {
    clear_screen();
    create_title(module.title);

    lv_obj_t* const label = lv_label_create(lv_screen_active());
    std::ostringstream text;
    text << "Not yet implemented\n" << module_type_name(module.type)
         << "\nLegacy commands are not run in this build.";
    lv_label_set_text(label, text.str().c_str());
    lv_obj_set_width(label, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    UiTheme::set_role(label, UiThemeRole::DimText);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -18);

    create_menu_content(screen_height() - button_height() - kMenuBottomMargin);
    create_menu_button("Back", ActionKind::Back);
}

void LegacyUi::show_list_item_pending(const core::LegacyModule& list, std::size_t list_index) {
    clear_screen();
    pending_list_id_ = list.id;
    const bool dynamic_item = list_index >= list.list_items.size();
    create_title(dynamic_item ? list.title : list.list_items[list_index].title);

    lv_obj_t* const label = lv_label_create(lv_screen_active());
    const char* const text = dynamic_item
        ? "Dynamic list sources are not yet implemented."
        : "This legacy action is pending an explicit compiler allowlist.\n"
          "No command was started.";
    lv_label_set_text(label, text);
    lv_obj_set_width(label, screen_width() - 2 * kHorizontalMargin);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    UiTheme::set_role(label, UiThemeRole::DimText);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -18);

    create_menu_content(screen_height() - button_height() - kMenuBottomMargin);
    create_menu_button("Back", ActionKind::Back);
}

void LegacyUi::show_parent_menu() {
    const auto parent_id = navigation_.back();
    if (!parent_id.has_value() || parent_id->empty()) {
        show_root();
        return;
    }
    const core::LegacyModule* const parent = config_.find(*parent_id);
    if (parent == nullptr || parent->type != core::LegacyModuleType::Menu) {
        show_root();
        return;
    }
    show_menu(*parent);
}

void LegacyUi::activate(const Action& action) {
    if (action.kind == ActionKind::Back) {
        if (!pending_list_id_.empty()) {
            const core::LegacyModule* const list = config_.find(pending_list_id_);
            if (list != nullptr && list->type == core::LegacyModuleType::GenericList) {
                show_generic_list(*list);
                return;
            }
            pending_list_id_.clear();
        }
        show_parent_menu();
        return;
    }
    if (action.kind == ActionKind::ListItem) {
        const core::LegacyModule* const list = config_.find(action.module_id);
        if (list == nullptr || list->type != core::LegacyModuleType::GenericList) {
            show_root();
            return;
        }
        show_list_item_pending(*list, action.list_index);
        return;
    }

    const core::LegacyModule* const module = config_.find(action.module_id);
    if (module == nullptr) {
        show_root();
        return;
    }
    if (module->type == core::LegacyModuleType::Menu) {
        navigation_.enter_menu(module->id);
        show_menu(*module);
        return;
    }
    navigation_.enter_leaf();
    if (module->type == core::LegacyModuleType::GenericList) {
        show_generic_list(*module);
        return;
    }
    show_not_implemented(*module);
}

void LegacyUi::queue_action(const Action& action) {
    auto pending = std::make_unique<Action>(action);
    Action* const raw_action = pending.get();
    if (lv_async_call(deferred_action_callback, raw_action) != LV_RESULT_OK) {
        activate(action);
        return;
    }
    pending_actions_.push_back(std::move(pending));
}

int LegacyUi::screen_width() const {
    return lv_display_get_horizontal_resolution(nullptr);
}

int LegacyUi::screen_height() const {
    return lv_display_get_vertical_resolution(nullptr);
}

int LegacyUi::button_height() const {
    return screen_height() > screen_width() ? 48 : 44;
}

void LegacyUi::action_callback(lv_event_t* event) {
    const auto* action = static_cast<const Action*>(lv_event_get_user_data(event));
    action->ui->queue_action(*action);
}

void LegacyUi::deferred_action_callback(void* user_data) {
    auto* const pending = static_cast<Action*>(user_data);
    LegacyUi* const ui = pending->ui;
    const auto found = std::find_if(ui->pending_actions_.begin(), ui->pending_actions_.end(),
                                    [pending](const auto& candidate) {
                                        return candidate.get() == pending;
                                    });
    if (found == ui->pending_actions_.end()) {
        return;
    }
    const Action action = **found;
    ui->pending_actions_.erase(found);
    ui->activate(action);
}

}  // namespace micropanel_touch::ui
