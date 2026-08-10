#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micropanel_touch::ui {

struct StarterMenuItem {
    std::string id;
    std::string title;
};

struct StarterModule {
    std::string id;
    std::string title;
    std::string type;
    bool enabled{false};
    std::vector<StarterMenuItem> submenus;
};

class StarterConfig {
public:
    static std::optional<StarterConfig> load(const std::filesystem::path& path,
                                             std::string* diagnostic);

    const StarterModule* find(const std::string& id) const;
    std::vector<const StarterModule*> root_menus() const;

private:
    std::vector<StarterModule> modules_;
};

}  // namespace micropanel_touch::ui
