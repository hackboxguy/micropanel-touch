#ifdef NDEBUG
#undef NDEBUG
#endif

#include "ui/StarterConfig.h"

#include <cassert>
#include <string>

int main(int argc, char* argv[]) {
    assert(argc == 2);
    std::string diagnostic;
    const auto config = micropanel_touch::ui::StarterConfig::load(argv[1], &diagnostic);
    assert(config.has_value());
    assert(config->theme() == "dark");
    assert(config->root_presentation().layout == micropanel_touch::ui::StarterMenuLayout::Grid);
    assert(config->root_presentation().columns == 2U);
    assert(config->root_menus().size() == 3U);
    const auto* network = config->find("network_menu");
    assert(network != nullptr);
    assert(network->presentation.layout == micropanel_touch::ui::StarterMenuLayout::List);
    assert(network->presentation.accent == "#2f7ea3");
    assert(network->icon == "network");
    assert(network->submenus.size() == 5U);
    assert(network->submenus.at(2).icon == "wifi");
    assert(network->submenus.at(2).color.empty());
    assert(network->submenus.at(3).color.empty());
    assert(config->find("netinfo") != nullptr);
    assert(config->find("progress_demo") != nullptr);
    assert(config->find("slider_demo") != nullptr);
    assert(config->find("wifi_password_demo") != nullptr);
    return 0;
}
