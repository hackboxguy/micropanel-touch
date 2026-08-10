#include "ui/StarterConfig.h"

#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace micropanel_touch::ui {
namespace {

bool is_hex_color(const std::string& value) {
    if (value.size() != 7U || value.front() != '#') {
        return false;
    }
    for (std::size_t index = 1; index < value.size(); ++index) {
        if (std::isxdigit(static_cast<unsigned char>(value[index])) == 0) {
            return false;
        }
    }
    return true;
}

std::string optional_color(const nlohmann::json& value, const std::string& scope) {
    if (!value.contains("color")) {
        return {};
    }
    const std::string color = value.at("color").get<std::string>();
    if (!is_hex_color(color)) {
        throw std::runtime_error(scope + " color must be a #RRGGBB value");
    }
    return color;
}

StarterMenuPresentation menu_presentation(const nlohmann::json& value,
                                          const std::string& scope) {
    StarterMenuPresentation presentation;
    if (value.contains("layout")) {
        const std::string layout = value.at("layout").get<std::string>();
        if (layout == "list") {
            presentation.layout = StarterMenuLayout::List;
        } else if (layout == "grid") {
            presentation.layout = StarterMenuLayout::Grid;
        } else {
            throw std::runtime_error(scope + " layout must be list or grid");
        }
    }
    if (value.contains("columns")) {
        if (!value.at("columns").is_number_unsigned()) {
            throw std::runtime_error(scope + " columns must be a positive integer");
        }
        const unsigned int columns = value.at("columns").get<unsigned int>();
        // On the narrow 320 px portrait panel, more than four columns would
        // violate the 48 px minimum touch target before a skin can intervene.
        if (columns == 0U || columns > 4U) {
            throw std::runtime_error(scope + " columns must be between 1 and 4");
        }
        presentation.columns = columns;
    }
    if (value.contains("accent")) {
        presentation.accent = value.at("accent").get<std::string>();
        if (!is_hex_color(presentation.accent)) {
            throw std::runtime_error(scope + " accent must be a #RRGGBB value");
        }
    }
    return presentation;
}

}  // namespace

std::optional<StarterConfig> StarterConfig::load(const std::filesystem::path& path,
                                                  std::string* diagnostic) {
    try {
        std::ifstream stream(path);
        if (!stream) {
            throw std::runtime_error("Cannot open " + path.string());
        }
        const nlohmann::json root = nlohmann::json::parse(stream);
        if (!root.contains("modules") || !root.at("modules").is_array()) {
            throw std::runtime_error("config has no modules array");
        }

        StarterConfig config;
        if (root.contains("theme")) {
            config.theme_ = root.at("theme").get<std::string>();
            if (config.theme_.empty()) {
                throw std::runtime_error("theme cannot be empty");
            }
        }
        if (root.contains("root")) {
            if (!root.at("root").is_object()) {
                throw std::runtime_error("root presentation is not an object");
            }
            config.root_presentation_ = menu_presentation(root.at("root"), "root presentation");
        }
        for (const auto& value : root.at("modules")) {
            if (!value.is_object()) {
                throw std::runtime_error("module is not an object");
            }
            StarterModule module;
            module.id = value.at("id").get<std::string>();
            module.title = value.value("title", module.id);
            module.type = value.value("type", "builtin");
            module.enabled = value.value("enabled", false);
            module.presentation = menu_presentation(value, "module " + module.id);
            if (value.contains("submenus")) {
                if (!value.at("submenus").is_array()) {
                    throw std::runtime_error("submenus for " + module.id + " is not an array");
                }
                for (const auto& submenu : value.at("submenus")) {
                    if (!submenu.is_object()) {
                        throw std::runtime_error("submenu for " + module.id + " is not an object");
                    }
                    const std::string id = submenu.at("id").get<std::string>();
                    module.submenus.push_back({id,
                                               submenu.value("title", id),
                                               submenu.value("icon", std::string{}),
                                               optional_color(submenu, "submenu " + id)});
                }
            }
            config.modules_.push_back(std::move(module));
        }
        return config;
    } catch (const std::exception& error) {
        if (diagnostic != nullptr) {
            *diagnostic = error.what();
        }
        return std::nullopt;
    }
}

const StarterModule* StarterConfig::find(const std::string& id) const {
    for (const auto& module : modules_) {
        if (module.id == id) {
            return &module;
        }
    }
    return nullptr;
}

std::vector<const StarterModule*> StarterConfig::root_menus() const {
    std::vector<const StarterModule*> menus;
    for (const auto& module : modules_) {
        if (module.enabled && module.type == "menu") {
            menus.push_back(&module);
        }
    }
    return menus;
}

const StarterMenuPresentation& StarterConfig::root_presentation() const {
    return root_presentation_;
}

const std::string& StarterConfig::theme() const {
    return theme_;
}

}  // namespace micropanel_touch::ui
