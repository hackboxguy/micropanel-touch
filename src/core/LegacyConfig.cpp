#include "core/LegacyConfig.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace micropanel_touch::core {
namespace {

using Json = nlohmann::json;

std::string require_string(const Json& value, const char* key, const std::string& scope) {
    if (!value.contains(key) || !value.at(key).is_string()) {
        throw std::runtime_error(scope + " requires string '" + key + "'");
    }
    const std::string parsed = value.at(key).get<std::string>();
    if (parsed.empty()) {
        throw std::runtime_error(scope + " '" + key + "' cannot be empty");
    }
    return parsed;
}

std::string optional_string(const Json& value, const char* key, const std::string& scope) {
    if (!value.contains(key)) {
        return {};
    }
    if (!value.at(key).is_string()) {
        throw std::runtime_error(scope + " '" + key + "' must be a string");
    }
    return value.at(key).get<std::string>();
}

bool optional_boolean(const Json& value, const char* key, bool default_value,
                      const std::string& scope) {
    if (!value.contains(key)) {
        return default_value;
    }
    if (!value.at(key).is_boolean()) {
        throw std::runtime_error(scope + " '" + key + "' must be a boolean");
    }
    return value.at(key).get<bool>();
}

std::optional<std::uint32_t> optional_positive_seconds(const Json& value, const char* key,
                                                        const std::string& scope) {
    if (!value.contains(key)) {
        return std::nullopt;
    }
    if (!value.at(key).is_number_unsigned()) {
        throw std::runtime_error(scope + " '" + key + "' must be a positive integer");
    }
    const std::uint64_t seconds = value.at(key).get<std::uint64_t>();
    if (seconds == 0U || seconds > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(scope + " '" + key + "' must be a positive integer");
    }
    return static_cast<std::uint32_t>(seconds);
}

LegacyModuleType parse_module_type(const Json& value, const std::string& scope) {
    if (!value.contains("type")) {
        return LegacyModuleType::Builtin;
    }
    if (!value.at("type").is_string()) {
        throw std::runtime_error(scope + " 'type' must be a string");
    }
    const std::string type = value.at("type").get<std::string>();
    if (type == "menu") {
        return LegacyModuleType::Menu;
    }
    if (type == "GenericList") {
        return LegacyModuleType::GenericList;
    }
    if (type == "textbox") {
        return LegacyModuleType::Textbox;
    }
    if (type == "action") {
        return LegacyModuleType::Action;
    }
    throw std::runtime_error(scope + " has unsupported type '" + type + "'");
}

LegacyMenuItem parse_menu_item(const Json& value, const std::string& scope) {
    if (!value.is_object()) {
        throw std::runtime_error(scope + " is not an object");
    }
    LegacyMenuItem item;
    item.id = require_string(value, "id", scope);
    item.title = optional_string(value, "title", scope);
    if (item.title.empty()) {
        item.title = item.id;
    }
    return item;
}

LegacyListItem parse_list_item(const Json& value, const std::string& scope) {
    if (!value.is_object()) {
        throw std::runtime_error(scope + " is not an object");
    }
    LegacyListItem item;
    item.title = require_string(value, "title", scope);
    item.action = optional_string(value, "action", scope);
    item.asynchronous = optional_boolean(value, "async", false, scope);
    item.timeout_seconds = optional_positive_seconds(value, "timeout", scope);
    item.log_file = optional_string(value, "log_file", scope);
    item.progress_title = optional_string(value, "progress_title", scope);
    item.result_pattern = optional_string(value, "result_pattern", scope);
    item.result_prefix = optional_string(value, "result_prefix", scope);
    item.usb_blaster_duration_seconds =
        optional_positive_seconds(value, "usb_blaster_duration", scope);
    item.parse_progress = optional_boolean(value, "parse_progress", false, scope);

    const bool has_execution_setting = item.asynchronous || item.timeout_seconds.has_value() ||
                                       !item.log_file.empty() || !item.progress_title.empty() ||
                                       !item.result_pattern.empty() || !item.result_prefix.empty() ||
                                       item.usb_blaster_duration_seconds.has_value() ||
                                       item.parse_progress;
    if (item.action.empty() && has_execution_setting) {
        throw std::runtime_error(scope + " has execution settings but no action");
    }
    return item;
}

void validate_menu_edges(const std::vector<LegacyModule>& modules,
                         const std::unordered_map<std::string, std::size_t>& index_by_id) {
    for (const LegacyModule& module : modules) {
        if (module.type != LegacyModuleType::Menu) {
            continue;
        }
        for (const LegacyMenuItem& item : module.submenus) {
            if (!item.is_back() && index_by_id.find(item.id) == index_by_id.end()) {
                throw std::runtime_error("menu '" + module.id + "' references unknown module '" +
                                         item.id + "'");
            }
        }
    }
}

void validate_menu_cycles(const std::vector<LegacyModule>& modules,
                          const std::unordered_map<std::string, std::size_t>& index_by_id) {
    enum class VisitState { Unvisited, Visiting, Visited };
    std::vector<VisitState> states(modules.size(), VisitState::Unvisited);
    std::vector<std::string> stack;

    const std::function<void(std::size_t)> visit = [&](std::size_t index) {
        VisitState& state = states.at(index);
        if (state == VisitState::Visiting) {
            const auto cycle_start = std::find(stack.begin(), stack.end(), modules.at(index).id);
            std::string cycle;
            for (auto current = cycle_start; current != stack.end(); ++current) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle += *current;
            }
            throw std::runtime_error("menu cycle: " + cycle + " -> " + modules.at(index).id);
        }
        if (state == VisitState::Visited) {
            return;
        }

        state = VisitState::Visiting;
        stack.push_back(modules.at(index).id);
        for (const LegacyMenuItem& item : modules.at(index).submenus) {
            if (item.is_back()) {
                continue;
            }
            const std::size_t target = index_by_id.at(item.id);
            if (modules.at(target).type == LegacyModuleType::Menu) {
                visit(target);
            }
        }
        stack.pop_back();
        state = VisitState::Visited;
    };

    for (std::size_t index = 0; index < modules.size(); ++index) {
        if (modules.at(index).type == LegacyModuleType::Menu) {
            visit(index);
        }
    }
}

}  // namespace

bool is_legacy_back_title(std::string_view title) {
    return title == "Back" || title == "back" || title == "BACK";
}

bool LegacyMenuItem::is_back() const {
    return id == "back";
}

bool LegacyListItem::is_back() const {
    return is_legacy_back_title(title);
}

bool LegacyModule::has_dynamic_items() const {
    return !items_source.empty();
}

std::optional<LegacyConfig> LegacyConfig::load(const std::filesystem::path& path,
                                                 std::string* diagnostic) {
    try {
        std::ifstream stream(path);
        if (!stream) {
            throw std::runtime_error("Cannot open " + path.string());
        }
        const Json root = Json::parse(stream);
        if (!root.is_object() || !root.contains("modules") || !root.at("modules").is_array()) {
            throw std::runtime_error("config has no modules array");
        }

        LegacyConfig config;
        std::unordered_map<std::string, std::size_t> index_by_id;
        for (const Json& value : root.at("modules")) {
            if (!value.is_object()) {
                throw std::runtime_error("module is not an object");
            }
            LegacyModule module;
            module.id = require_string(value, "id", "module");
            const std::string scope = "module '" + module.id + "'";
            if (!index_by_id.emplace(module.id, config.modules_.size()).second) {
                throw std::runtime_error("duplicate module id '" + module.id + "'");
            }
            module.title = optional_string(value, "title", scope);
            if (module.title.empty()) {
                module.title = module.id;
            }
            module.type = parse_module_type(value, scope);
            module.enabled = optional_boolean(value, "enabled", false, scope);

            if (module.type == LegacyModuleType::Menu) {
                if (!value.contains("submenus") || !value.at("submenus").is_array() ||
                    value.at("submenus").empty()) {
                    throw std::runtime_error(scope + " requires a non-empty submenus array");
                }
                std::size_t item_index = 0;
                for (const Json& item : value.at("submenus")) {
                    module.submenus.push_back(
                        parse_menu_item(item, scope + " submenu " + std::to_string(item_index++)));
                }
            }
            if (module.type == LegacyModuleType::GenericList) {
                module.items_source = optional_string(value, "items_source", scope);
                module.items_action = optional_string(value, "items_action", scope);
                module.list_selection = optional_string(value, "list_selection", scope);
                module.prepend_static_items =
                    optional_boolean(value, "prepend_static_items", false, scope);
                module.items_path = optional_string(value, "items_path", scope);

                bool has_static_items = false;
                if (value.contains("list_items")) {
                    if (!value.at("list_items").is_array()) {
                        throw std::runtime_error(scope + " 'list_items' must be an array");
                    }
                    has_static_items = !value.at("list_items").empty();
                }
                std::size_t item_index = 0;
                if (has_static_items) {
                    for (const Json& item : value.at("list_items")) {
                        module.list_items.push_back(
                            parse_list_item(item, scope + " list item " + std::to_string(item_index++)));
                    }
                }
                if (!has_static_items && !module.has_dynamic_items()) {
                    throw std::runtime_error(scope +
                                             " requires a non-empty list_items array or items_source");
                }
            }
            config.modules_.push_back(std::move(module));
        }

        validate_menu_edges(config.modules_, index_by_id);
        validate_menu_cycles(config.modules_, index_by_id);
        return config;
    } catch (const std::exception& error) {
        if (diagnostic != nullptr) {
            *diagnostic = error.what();
        }
        return std::nullopt;
    }
}

const LegacyModule* LegacyConfig::find(const std::string& id) const {
    const auto found = std::find_if(modules_.begin(), modules_.end(), [&id](const LegacyModule& module) {
        return module.id == id;
    });
    return found == modules_.end() ? nullptr : &*found;
}

std::vector<const LegacyModule*> LegacyConfig::root_modules() const {
    std::vector<const LegacyModule*> roots;
    for (const LegacyModule& module : modules_) {
        if (module.enabled) {
            roots.push_back(&module);
        }
    }
    return roots;
}

const std::vector<LegacyModule>& LegacyConfig::modules() const {
    return modules_;
}

LegacyConfigCounts LegacyConfig::counts() const {
    LegacyConfigCounts counts;
    counts.module_declarations = modules_.size();
    for (const LegacyModule& module : modules_) {
        counts.submenu_references += module.submenus.size();
    }
    return counts;
}

}  // namespace micropanel_touch::core
