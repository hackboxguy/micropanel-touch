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

// A `"type": "textbox"` module: a read-only status screen that shows what a
// script prints, refreshed every refresh_ms while the screen is open. The keys
// live under `depends`, as in the legacy micropanel config (script_path,
// refresh_sec, display_title). `script_path` may start with $MICROPANEL_HOME.
// A `"type": "house"` module uses the same keys, but its script prints one JSON
// object that is drawn as a small house scene (see StarterUi::render_house).
struct StarterTextbox {
    std::string script_path;
    std::vector<std::string> arguments;
    unsigned int refresh_ms{5000U};
    unsigned int timeout_ms{10000U};
    std::string display_title;
};

struct StarterModule {
    std::string id;
    std::string title;
    std::string type;
    std::string icon;
    bool enabled{false};
    StarterMenuPresentation presentation;
    std::vector<StarterMenuItem> submenus;
    StarterTextbox textbox;  // only meaningful for type "textbox"
};

class StarterConfig {
public:
    static std::optional<StarterConfig> load(const std::filesystem::path& path,
                                             std::string* diagnostic);

    const StarterModule* find(const std::string& id) const;
    std::vector<const StarterModule*> root_menus() const;
    const StarterMenuPresentation& root_presentation() const;
    const std::string& theme() const;
    // Root screen heading (`root.title`), "MicroPanel Touch" by default.
    const std::string& root_title() const;
    // Optional top-level `"home"`: id of an enabled menu or textbox module
    // shown at start-up and on standby, instead of the root tiles.
    const std::string& home_module() const;
    unsigned int display_sleep_seconds() const;
    // `power.blank_without_backlight`: allow display standby on a panel with no
    // backlight control by blanking the framebuffer (black, backlight stays on).
    bool blank_without_backlight() const;

private:
    StarterMenuPresentation root_presentation_;
    std::string theme_{"dark"};
    std::string root_title_{"MicroPanel Touch"};
    std::string home_module_;
    unsigned int display_sleep_seconds_{60U};
    bool blank_without_backlight_{false};
    std::vector<StarterModule> modules_;
};

}  // namespace micropanel_touch::ui
