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
    unsigned int rows{2};
    std::string accent;
};

struct StarterMenuItem {
    std::string id;
    std::string title;
    std::string icon;
    std::string color;
    // Defaults to true, unlike a module's `enabled`, which defaults to false.
    // The asymmetry is deliberate: a module must opt in to appearing on the
    // root screen, whereas an entry someone has written into a submenus array
    // is plainly meant to be shown unless it is explicitly switched off. A
    // disabled entry stays in the config as a record of a feature that exists
    // but is not wired up yet, and is re-enabled by flipping one boolean.
    bool enabled{true};
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
