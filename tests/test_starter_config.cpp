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
    assert(shown(*network) == 5U);                       // Status, IP Settings, WiFi, Testing, Back
    // The id stays netinfo: it is one of the type-less built-ins the legacy
    // config contract names (PRD SS7), and the deferred parity work still
    // resolves against it. Only what a person reads changed.
    assert(network->submenus.at(0).id == "netinfo");
    assert(network->submenus.at(0).title == "Status");
    // The Wi-Fi password screen is no longer a menu entry of its own. It used
    // to be a hidden demo tile; it is now reached from the network list, which
    // is both how a person would look for it and what makes the redaction
    // tests cover the real join path.
    assert(config->find("wifi_password_demo") == nullptr);
    assert(network->submenus.at(3).id == "nettest");
    assert(network->submenus.at(4).id == "back");
    assert(network->submenus.at(4).icon == "back");      // grid tiles all need one
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
    // the demos switched off. Seven visible tiles need four rows - the tested
    // no-scroll grid is raised rather than letting a tile scroll out of reach,
    // and the fourth row is what leaves room for the remaining base features.
    const auto* system_menu = config->find("system_menu");
    assert(system_menu != nullptr);
    assert(system_menu->presentation.layout == micropanel_touch::ui::StarterMenuLayout::Grid);
    assert(system_menu->presentation.rows == 4U);
    assert(shown(*system_menu) == 8U);                   // exactly fills the 2x4 grid
    assert(system_menu->submenus.at(0).id == "system");
    assert(system_menu->submenus.at(0).title == "System Stats");
    assert(system_menu->submenus.at(0).enabled);         // wired for real now
    assert(system_menu->submenus.at(1).id == "about");
    assert(system_menu->submenus.at(1).enabled);
    assert(config->find("about") != nullptr);
    assert(config->find("power") != nullptr);
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
    return 0;
}
