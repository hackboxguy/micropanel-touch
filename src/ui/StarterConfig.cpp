#include "ui/StarterConfig.h"

#include <cctype>
#include <string>
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
    if (value.contains("rows")) {
        if (!value.at("rows").is_number_unsigned()) {
            throw std::runtime_error(scope + " rows must be a positive integer");
        }
        const unsigned int rows = value.at("rows").get<unsigned int>();
        // The 320x480 portrait panel retains a practical 56 px tile height
        // for up to six rows after gaps are accounted for.
        if (rows == 0U || rows > 6U) {
            throw std::runtime_error(scope + " rows must be between 1 and 6");
        }
        presentation.rows = rows;
    }
    if (value.contains("accent")) {
        presentation.accent = value.at("accent").get<std::string>();
        if (!is_hex_color(presentation.accent)) {
            throw std::runtime_error(scope + " accent must be a #RRGGBB value");
        }
    }
    return presentation;
}

// Seconds as a JSON number or a numeric string ("2.0" in the legacy configs).
double seconds_value(const nlohmann::json& value, const std::string& scope) {
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        std::size_t used = 0;
        double seconds = 0.0;
        try {
            seconds = std::stod(text, &used);
        } catch (const std::exception&) {
            used = 0;
        }
        if (used == text.size() && used != 0U) {
            return seconds;
        }
    }
    throw std::runtime_error(scope + " must be a number of seconds");
}

StarterTextbox textbox_settings(const nlohmann::json& value, const std::string& scope) {
    if (!value.contains("depends") || !value.at("depends").is_object()) {
        throw std::runtime_error(scope + " needs a depends object with script_path");
    }
    const auto& depends = value.at("depends");
    StarterTextbox textbox;
    if (!depends.contains("script_path") || !depends.at("script_path").is_string() ||
        depends.at("script_path").get<std::string>().empty()) {
        throw std::runtime_error(scope + " depends.script_path must be a non-empty string");
    }
    textbox.script_path = depends.at("script_path").get<std::string>();
    if (depends.contains("args")) {
        if (!depends.at("args").is_array()) {
            throw std::runtime_error(scope + " depends.args must be an array of strings");
        }
        for (const auto& argument : depends.at("args")) {
            if (!argument.is_string()) {
                throw std::runtime_error(scope + " depends.args must be an array of strings");
            }
            textbox.arguments.push_back(argument.get<std::string>());
        }
    }
    if (depends.contains("refresh_sec")) {
        const double seconds = seconds_value(depends.at("refresh_sec"), scope + " depends.refresh_sec");
        // Every refresh is a process start and, on an SPI panel, a redraw.
        if (seconds < 1.0 || seconds > 3600.0) {
            throw std::runtime_error(scope + " depends.refresh_sec must be between 1 and 3600");
        }
        textbox.refresh_ms = static_cast<unsigned int>(seconds * 1000.0);
    }
    if (depends.contains("timeout_sec")) {
        const double seconds = seconds_value(depends.at("timeout_sec"), scope + " depends.timeout_sec");
        if (seconds < 1.0 || seconds > 60.0) {
            throw std::runtime_error(scope + " depends.timeout_sec must be between 1 and 60");
        }
        textbox.timeout_ms = static_cast<unsigned int>(seconds * 1000.0);
    }
    if (depends.contains("display_title")) {
        textbox.display_title = depends.at("display_title").get<std::string>();
    }
    return textbox;
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
            if (root.at("root").contains("title")) {
                config.root_title_ = root.at("root").at("title").get<std::string>();
            }
        }
        if (root.contains("power")) {
            const auto& power = root.at("power");
            if (!power.is_object()) {
                throw std::runtime_error("power is not an object");
            }
            if (power.contains("display_sleep_sec")) {
                if (!power.at("display_sleep_sec").is_number_unsigned()) {
                    throw std::runtime_error("power display_sleep_sec must be a non-negative integer");
                }
                const unsigned int seconds = power.at("display_sleep_sec").get<unsigned int>();
                if (seconds != 0U && (seconds < 10U || seconds > 180U || seconds % 10U != 0U)) {
                    throw std::runtime_error(
                        "power display_sleep_sec must be 0 or a 10-second step from 10 to 180");
                }
                config.display_sleep_seconds_ = seconds;
            }
            if (power.contains("blank_without_backlight")) {
                if (!power.at("blank_without_backlight").is_boolean()) {
                    throw std::runtime_error("power blank_without_backlight must be true or false");
                }
                config.blank_without_backlight_ = power.at("blank_without_backlight").get<bool>();
            }
        }
        for (const auto& value : root.at("modules")) {
            if (!value.is_object()) {
                throw std::runtime_error("module is not an object");
            }
            StarterModule module;
            module.id = value.at("id").get<std::string>();
            module.title = value.value("title", module.id);
            module.type = value.value("type", "builtin");
            module.icon = value.value("icon", std::string{});
            module.enabled = value.value("enabled", false);
            module.presentation = menu_presentation(value, "module " + module.id);
            if (module.type == "textbox") {
                module.textbox = textbox_settings(value, "textbox " + module.id);
            }
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
                                               optional_color(submenu, "submenu " + id),
                                               submenu.value("enabled", true)});
                }
            }
            config.modules_.push_back(std::move(module));
        }
        if (root.contains("home")) {
            config.home_module_ = root.at("home").get<std::string>();
            const StarterModule* const home = config.find(config.home_module_);
            if (home == nullptr || !home->enabled ||
                (home->type != "menu" && home->type != "textbox")) {
                throw std::runtime_error("home must name an enabled menu or textbox module");
            }
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
        // Textbox screens can sit on the root grid directly, next to menus.
        if (module.enabled && (module.type == "menu" || module.type == "textbox")) {
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

const std::string& StarterConfig::root_title() const {
    return root_title_;
}

const std::string& StarterConfig::home_module() const {
    return home_module_;
}

unsigned int StarterConfig::display_sleep_seconds() const {
    return display_sleep_seconds_;
}

bool StarterConfig::blank_without_backlight() const {
    return blank_without_backlight_;
}

}  // namespace micropanel_touch::ui
