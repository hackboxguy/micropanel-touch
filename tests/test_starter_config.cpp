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
    assert(network->submenus.size() == 4U);
    assert(network->submenus.at(2).icon == "wifi");
    assert(network->submenus.at(2).color == "#3d9bf0");
    assert(config->find("netinfo") != nullptr);
    return 0;
}
