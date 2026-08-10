#pragma once

#include <optional>
#include <string>
#include <vector>

namespace micropanel_touch::core {

/**
 * Tracks the menu context behind transient leaf screens. Empty denotes the
 * root menu. It is UI-toolkit independent so the Sprint 2 navigator can keep
 * the same observable Back behavior.
 */
class NavigationHistory {
public:
    void reset();
    void enter_menu(std::string menu_id);
    void enter_leaf();
    std::optional<std::string> back();

    const std::string& current_menu_id() const;
    std::vector<std::string> menu_path() const;

private:
    std::string current_menu_id_;
    std::vector<std::string> parents_;
};

}  // namespace micropanel_touch::core
