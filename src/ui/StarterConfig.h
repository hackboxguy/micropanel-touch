#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micropanel_touch::ui {

enum class StarterMenuLayout {
    List,
    Grid,
};

struct StarterMenuPresentation {
    StarterMenuLayout layout{StarterMenuLayout::List};
    unsigned int columns{2};
    std::string accent;
};

struct StarterMenuItem {
    std::string id;
    std::string title;
    std::string icon;
    std::string color;
};

struct StarterModule {
    std::string id;
    std::string title;
    std::string type;
    std::string icon;
    bool enabled{false};
    StarterMenuPresentation presentation;
    std::vector<StarterMenuItem> submenus;
};

class StarterConfig {
public:
    static std::optional<StarterConfig> load(const std::filesystem::path& path,
                                             std::string* diagnostic);

    const StarterModule* find(const std::string& id) const;
    std::vector<const StarterModule*> root_menus() const;
    const StarterMenuPresentation& root_presentation() const;
    const std::string& theme() const;
    unsigned int display_sleep_seconds() const;

private:
    StarterMenuPresentation root_presentation_;
    std::string theme_{"dark"};
    unsigned int display_sleep_seconds_{60U};
    std::vector<StarterModule> modules_;
};

}  // namespace micropanel_touch::ui
