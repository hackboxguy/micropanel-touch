#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/NavigationHistory.h"

#include <cassert>

int main() {
    micropanel_touch::core::NavigationHistory navigation;

    navigation.enter_menu("network");
    navigation.enter_leaf();
    assert(navigation.back() == "network");
    assert(navigation.current_menu_id() == "network");
    assert(navigation.back() == "");
    assert(navigation.current_menu_id().empty());

    navigation.enter_menu("display");
    navigation.enter_menu("advanced");
    assert(navigation.back() == "display");
    assert(navigation.back() == "");
    assert(!navigation.back().has_value());

    navigation.enter_menu("system");
    navigation.reset();
    assert(navigation.current_menu_id().empty());
    assert(!navigation.back().has_value());
    return 0;
}
