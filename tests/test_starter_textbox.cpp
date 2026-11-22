#ifdef NDEBUG
#undef NDEBUG
#endif

#include "ui/StarterConfig.h"

#include <cassert>
#include <filesystem>
#include <string>

using micropanel_touch::ui::StarterConfig;

int main(int argc, char* argv[]) {
    assert(argc == 2);
    const std::filesystem::path fixtures(argv[1]);
    std::string diagnostic;

    const auto config = StarterConfig::load(fixtures / "valid.json", &diagnostic);
    assert(config.has_value());
    assert(config->root_title() == "Cat House");
    assert(config->home_module() == "status");
    assert(config->blank_without_backlight());
    const auto* status = config->find("status");
    assert(status != nullptr && status->type == "textbox");
    assert(status->textbox.script_path == "/usr/local/bin/status");
    assert(status->textbox.arguments.size() == 1U && status->textbox.arguments[0] == "--short");
    assert(status->textbox.refresh_ms == 10000U);
    assert(status->textbox.timeout_ms == 5000U);
    assert(status->textbox.display_title == "Cat house");
    // Legacy spelling: numeric string, $MICROPANEL_HOME kept for run time.
    const auto* legacy = config->find("legacy");
    assert(legacy != nullptr && legacy->textbox.refresh_ms == 2000U);
    assert(legacy->textbox.script_path.rfind("$MICROPANEL_HOME/", 0U) == 0U);
    // Enabled textboxes sit on the root grid next to menus; disabled ones do not.
    const auto roots = config->root_menus();
    assert(roots.size() == 3U && roots[0]->id == "status" && roots[1]->id == "house" &&
           roots[2]->id == "system_menu");
    // A house scene takes the same script keys as a textbox.
    const auto* house = config->find("house");
    assert(house != nullptr && house->type == "house");
    assert(house->textbox.script_path == "/usr/local/bin/house" && house->textbox.refresh_ms == 15000U);

    for (const char* invalid : {"no-script.json", "fast-refresh.json", "bad-home.json",
                                "disabled-home.json", "bad-blank.json", "house-no-script.json"}) {
        diagnostic.clear();
        assert(!StarterConfig::load(fixtures / invalid, &diagnostic).has_value());
        assert(!diagnostic.empty());
    }

    return 0;
}
