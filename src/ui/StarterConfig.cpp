#include "ui/StarterConfig.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace micropanel_touch::ui {

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
        for (const auto& value : root.at("modules")) {
            if (!value.is_object()) {
                throw std::runtime_error("module is not an object");
            }
            StarterModule module;
            module.id = value.at("id").get<std::string>();
            module.title = value.value("title", module.id);
            module.type = value.value("type", "builtin");
            module.enabled = value.value("enabled", false);
            if (value.contains("submenus")) {
                if (!value.at("submenus").is_array()) {
                    throw std::runtime_error("submenus for " + module.id + " is not an array");
                }
                for (const auto& submenu : value.at("submenus")) {
                    module.submenus.push_back({submenu.at("id").get<std::string>(),
                                               submenu.value("title", submenu.at("id").get<std::string>())});
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

}  // namespace micropanel_touch::ui
