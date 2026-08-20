#ifdef NDEBUG
#undef NDEBUG
#endif

#include "ui/StarterConfig.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    assert(argc == 2);
    std::string diagnostic;
    const auto config = micropanel_touch::ui::StarterConfig::load(argv[1], &diagnostic);
    assert(config.has_value());
    assert(config->theme() == "dark");
    assert(config->root_presentation().layout == micropanel_touch::ui::StarterMenuLayout::Grid);
    assert(config->root_presentation().columns == 2U);
    assert(config->display_sleep_seconds() == 60U);
    assert(config->root_menus().size() == 3U);
    const auto* network = config->find("network_menu");
    assert(network != nullptr);
    assert(network->presentation.layout == micropanel_touch::ui::StarterMenuLayout::Grid);
    assert(network->presentation.accent == "#2f7ea3");
    assert(network->icon == "network");
    assert(network->submenus.size() == 5U);
    assert(network->submenus.at(2).icon == "wifi");
    assert(network->submenus.at(2).color.empty());
    assert(network->submenus.at(3).color.empty());

    // What the shipping config actually offers. A disabled entry stays in the
    // file - it is a record of a feature that exists but is not wired up - so
    // these assertions are about `enabled`, not about the entry being absent.
    const auto shown = [](const micropanel_touch::ui::StarterModule& menu) {
        std::size_t count = 0;
        for (const auto& item : menu.submenus) {
            if (item.enabled) {
                ++count;
            }
        }
        return count;
    };
    assert(shown(*network) == 4U);                       // Info, IP Settings, WiFi, Back
    assert(network->submenus.at(4).id == "wifi_password_demo");
    assert(!network->submenus.at(4).enabled);            // experimental, hidden
    assert(network->submenus.at(3).id == "back");
    assert(network->submenus.at(3).icon == "back");      // grid tiles all need one
    const auto* display = config->find("display_menu");
    assert(display != nullptr);
    assert(display->presentation.rows == 3U);
    assert(display->submenus.at(0).icon == "brightness");
    assert(display->submenus.at(1).title == "Standby");
    assert(display->submenus.at(1).icon == "standby");
    assert(display->submenus.at(2).icon == "theme");
    assert(display->submenus.at(3).icon == "orientation");
    assert(!display->submenus.at(3).enabled);            // not implemented yet
    assert(display->submenus.at(4).icon == "back");
    assert(shown(*display) == 4U);

    // System: square tiles, every visible one carrying a distinct icon, with
    // the demos and the unimplemented stats screens switched off.
    const auto* system_menu = config->find("system_menu");
    assert(system_menu != nullptr);
    assert(system_menu->presentation.layout == micropanel_touch::ui::StarterMenuLayout::Grid);
    assert(shown(*system_menu) == 5U);
    std::vector<std::string> system_icons;
    for (const auto& item : system_menu->submenus) {
        if (item.enabled) {
            assert(!item.icon.empty());
            system_icons.push_back(item.icon);
        }
    }
    std::sort(system_icons.begin(), system_icons.end());
    assert(std::adjacent_find(system_icons.begin(), system_icons.end()) == system_icons.end());
    assert(config->find("netinfo") != nullptr);
    assert(config->find("progress_demo") != nullptr);
    assert(config->find("slider_demo") != nullptr);
    assert(config->find("wifi_password_demo") != nullptr);
    return 0;
}
