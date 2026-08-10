#include "core/NavigationHistory.h"

#include <utility>

namespace micropanel_touch::core {

void NavigationHistory::reset() {
    current_menu_id_.clear();
    parents_.clear();
}

void NavigationHistory::enter_menu(std::string menu_id) {
    parents_.push_back(current_menu_id_);
    current_menu_id_ = std::move(menu_id);
}

void NavigationHistory::enter_leaf() {
    parents_.push_back(current_menu_id_);
}

std::optional<std::string> NavigationHistory::back() {
    if (parents_.empty()) {
        return std::nullopt;
    }
    current_menu_id_ = std::move(parents_.back());
    parents_.pop_back();
    return current_menu_id_;
}

const std::string& NavigationHistory::current_menu_id() const {
    return current_menu_id_;
}

std::vector<std::string> NavigationHistory::menu_path() const {
    std::vector<std::string> path;
    path.reserve(parents_.size() + 1U);
    for (const std::string& parent : parents_) {
        if (!parent.empty()) {
            path.push_back(parent);
        }
    }
    if (!current_menu_id_.empty() && (path.empty() || path.back() != current_menu_id_)) {
        path.push_back(current_menu_id_);
    }
    return path;
}

}  // namespace micropanel_touch::core
