#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micropanel_touch::core {

enum class LegacyModuleType {
    Builtin,
    Menu,
    GenericList,
    Textbox,
    Action,
};

struct LegacyMenuItem {
    std::string id;
    std::string title;

    // Menus use the reserved id, unlike GenericList's title-based rule.
    bool is_back() const;
};

struct LegacyListItem {
    std::string title;
    std::string action;
    bool asynchronous{false};
    std::optional<std::uint32_t> timeout_seconds;
    std::string log_file;
    std::string progress_title;
    std::string result_pattern;
    std::string result_prefix;
    std::optional<std::uint32_t> usb_blaster_duration_seconds;
    bool parse_progress{false};

    // The OLED implementation treats Back/back/BACK by title as navigation,
    // even when no reserved id is present.
    bool is_back() const;
};

struct LegacyModule {
    std::string id;
    std::string title;
    LegacyModuleType type{LegacyModuleType::Builtin};
    bool enabled{false};
    std::vector<LegacyMenuItem> submenus;
    std::vector<LegacyListItem> list_items;
    // Dynamic-list fields are parsed now so the validator does not silently
    // discard a supported legacy key before its Sprint 4 executor arrives.
    std::string items_source;
    std::string items_action;
    std::string list_selection;
    bool prepend_static_items{false};
    std::string items_path;

    bool has_dynamic_items() const;
};

/**
 * Renderer-independent representation of the legacy JSON navigation surface.
 *
 * This first Sprint 2 loader owns menus and static GenericLists. It retains
 * dynamic-list declarations and the remaining shipped module categories so
 * disabled, reachable modules and accepted-pending keys are never silently
 * discarded before their renderers/executors arrive in later sprints.
 */
class LegacyConfig {
public:
    static std::optional<LegacyConfig> load(const std::filesystem::path& path,
                                             std::string* diagnostic);

    const LegacyModule* find(const std::string& id) const;

    // `enabled` controls registration at the root only. Disabled modules stay
    // addressable through a menu edge, matching the observable OLED behavior.
    std::vector<const LegacyModule*> root_modules() const;

    const std::vector<LegacyModule>& modules() const;

private:
    std::vector<LegacyModule> modules_;
};

bool is_legacy_back_title(std::string_view title);

}  // namespace micropanel_touch::core
