#include "ui/LegacyUi.h"

#include "ui/BuiltinIcon.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace micropanel_touch::ui {
namespace {

constexpr int kHorizontalMargin = 16;
constexpr int kMenuBottomMargin = 12;
constexpr int kMenuGap = 8;
constexpr std::size_t kMaximumWidgetSnapshots = 256U;
constexpr std::size_t kMaximumWidgetTextBytes = 256U;

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

}  // namespace

LegacyUi::LegacyUi(const core::LegacyConfig& config, core::UiEventQueue& event_queue)
    : config_(config), event_queue_(event_queue) {}

LegacyUi::~LegacyUi() {
    for (const auto& action : pending_actions_) {
        lv_async_call_cancel(deferred_action_callback, action.get());
    }
    if (control_timer_ != nullptr) {
        lv_timer_delete(control_timer_);
    }
}

void LegacyUi::start() {
    control_timer_ = lv_timer_create(control_timer_callback, 20, this);
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
    screen_id_ = "root";
    create_title("MicroPanel Touch");
    create_menu_content();
    for (const core::LegacyModule* module : config_.root_modules()) {
        create_menu_button(module->title, ActionKind::Module, module->id);
    }
}

void LegacyUi::show_menu(const core::LegacyModule& menu) {
    clear_screen();
    pending_list_id_.clear();
    screen_id_ = menu.id;
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
    screen_id_ = list.id;
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
    screen_id_ = module.id;
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
    screen_id_ = list.id + ":item:" + std::to_string(list_index);
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

core::UiControlResponse LegacyUi::state_response() const {
    return {true, screen_id_, navigation_.menu_path(), {}, false, {}};
}

void LegacyUi::settle_render() const {
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(lv_display_get_default());
}

void LegacyUi::append_widget_snapshots(lv_obj_t* object, std::int32_t parent_id,
                                       bool ancestor_redacted, std::uint32_t* next_id,
                                       core::UiControlResponse* response) const {
    if (object == nullptr || response->widgets.size() >= kMaximumWidgetSnapshots) {
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

    // A textarea owns an internal label containing its complete text. Do not
    // recurse into it: this avoids an accidental secret leak if a future UI
    // uses this capture command while a password field is visible.
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

std::vector<std::string> LegacyUi::path_to_module(const std::string& target) const {
    struct PendingPath {
        std::string id;
        std::vector<std::string> path;
    };

    std::vector<PendingPath> pending;
    std::unordered_set<std::string> visited;
    for (const core::LegacyModule* root : config_.root_modules()) {
        pending.push_back({root->id, {root->id}});
    }
    for (std::size_t index = 0U; index < pending.size(); ++index) {
        PendingPath current = std::move(pending[index]);
        if (!visited.insert(current.id).second) {
            continue;
        }
        if (current.id == target) {
            return current.path;
        }
        const core::LegacyModule* const module = config_.find(current.id);
        if (module == nullptr || module->type != core::LegacyModuleType::Menu) {
            continue;
        }
        for (const auto& item : module->submenus) {
            if (!item.is_back()) {
                PendingPath child{item.id, current.path};
                child.path.push_back(item.id);
                pending.push_back(std::move(child));
            }
        }
    }
    return {};
}

bool LegacyUi::activate_current_target(const std::string& target, std::string* diagnostic) {
    if (screen_id_ == "root") {
        for (const core::LegacyModule* root : config_.root_modules()) {
            if (root->id == target) {
                activate({this, ActionKind::Module, root->id, 0U});
                return true;
            }
        }
        *diagnostic = "target is not an enabled root module";
        return false;
    }

    const core::LegacyModule* const module = config_.find(screen_id_);
    if (module == nullptr || module->type == core::LegacyModuleType::Builtin ||
        module->type == core::LegacyModuleType::Textbox ||
        module->type == core::LegacyModuleType::Action) {
        *diagnostic = "current screen has no activatable items";
        return false;
    }
    if (module->type == core::LegacyModuleType::Menu) {
        for (const auto& item : module->submenus) {
            if (item.id == target) {
                activate({this, item.is_back() ? ActionKind::Back : ActionKind::Module,
                          item.id, 0U});
                return true;
            }
        }
        *diagnostic = "target is not in the current menu";
        return false;
    }
    if (target.rfind("list:", 0U) != 0U) {
        *diagnostic = "generic-list targets use list:N";
        return false;
    }
    std::size_t list_index = 0U;
    const std::string index_text = target.substr(std::char_traits<char>::length("list:"));
    const auto conversion = std::from_chars(index_text.data(), index_text.data() + index_text.size(),
                                            list_index);
    if (conversion.ec != std::errc{} || conversion.ptr != index_text.data() + index_text.size() ||
        list_index >= module->list_items.size()) {
        *diagnostic = "generic-list target is outside the visible list";
        return false;
    }
    const core::LegacyListItem& item = module->list_items[list_index];
    activate({this, item.is_back() ? ActionKind::Back : ActionKind::ListItem, module->id,
              list_index});
    return true;
}

core::UiControlResponse LegacyUi::handle_control(const core::UiControlCommand& command) {
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
        }
        return response;
    }
    if (command.type == core::UiControlCommandType::Back) {
        activate({this, ActionKind::Back, {}, 0U});
    } else if (command.type == core::UiControlCommandType::Navigate) {
        const std::vector<std::string> path = path_to_module(command.target);
        if (path.empty()) {
            return {false, {}, {}, {}, false, "target is not reachable from the enabled root"};
        }
        show_root();
        for (const std::string& id : path) {
            activate({this, ActionKind::Module, id, 0U});
        }
    } else {
        std::string diagnostic;
        if (!activate_current_target(command.target, &diagnostic)) {
            return {false, {}, {}, {}, false, std::move(diagnostic)};
        }
    }

    settle_render();
    return state_response();
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

void LegacyUi::control_timer_callback(lv_timer_t* timer) {
    auto* const ui = static_cast<LegacyUi*>(lv_timer_get_user_data(timer));
    for (auto& event : ui->event_queue_.drain()) {
        auto* const request = std::get_if<core::UiControlRequest>(&event.payload);
        if (request == nullptr || request->completion == nullptr) {
            continue;
        }
        try {
            request->completion->set_value(ui->handle_control(request->command));
        } catch (const std::future_error&) {
            // A timed-out client may have disconnected; the UI request was
            // still valid, but there is no reply receiver left to notify.
        }
    }
}

}  // namespace micropanel_touch::ui
