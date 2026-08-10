#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/LegacyConfig.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using micropanel_touch::core::LegacyConfig;
using micropanel_touch::core::LegacyModuleType;

namespace {

void expect_invalid(const std::string& contents, const std::string& expected_diagnostic) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("micropanel-touch-legacy-config-" +
                                        std::to_string(getpid()) + ".json");
    {
        std::ofstream stream(path);
        assert(stream);
        stream << contents;
    }
    std::string diagnostic;
    const auto config = LegacyConfig::load(path, &diagnostic);
    std::filesystem::remove(path);
    assert(!config.has_value());
    assert(diagnostic.find(expected_diagnostic) != std::string::npos);
}

}  // namespace

int main(int argc, char* argv[]) {
    assert(argc == 2 || argc == 3);
    std::string diagnostic;
    const auto config = LegacyConfig::load(argv[1], &diagnostic);
    assert(config.has_value());
    assert(config->modules().size() == 7U);

    const auto roots = config->root_modules();
    assert(roots.size() == 2U);
    assert(roots[0]->id == "root_menu");
    assert(roots[1]->id == "flash_actions");
    assert(config->find("disabled_menu") != nullptr);
    assert(config->find("disabled_menu")->type == LegacyModuleType::Menu);
    assert(config->find("textbox_info")->type == LegacyModuleType::Textbox);
    assert(config->find("action_info")->type == LegacyModuleType::Action);
    assert(config->find("builtin_status")->type == LegacyModuleType::Builtin);

    const auto* root_menu = config->find("root_menu");
    assert(root_menu->submenus.size() == 3U);
    assert(root_menu->submenus.back().is_back());
    assert(root_menu->submenus.back().title == "Return");

    const auto* flash_actions = config->find("flash_actions");
    assert(flash_actions->list_items.size() == 2U);
    const auto& update = flash_actions->list_items.front();
    assert(update.asynchronous);
    assert(update.timeout_seconds == 350U);
    assert(update.usb_blaster_duration_seconds == 103U);
    assert(update.parse_progress);
    assert(flash_actions->list_items.back().is_back());
    assert(!micropanel_touch::core::is_legacy_back_title("bAcK"));

    const auto* dynamic_actions = config->find("dynamic_actions");
    assert(dynamic_actions->list_items.empty());
    assert(dynamic_actions->has_dynamic_items());
    assert(dynamic_actions->items_action.find("$1") != std::string::npos);
    assert(dynamic_actions->list_selection.find("current-image") != std::string::npos);
    assert(dynamic_actions->prepend_static_items);
    assert(dynamic_actions->items_path.find("media/images") != std::string::npos);

    expect_invalid(
        R"({"modules":[{"id":"root","type":"menu","submenus":[{"id":"missing"}]}]})",
        "references unknown module");
    expect_invalid(
        R"({"modules":[{"id":"a","type":"menu","submenus":[{"id":"b"}]},{"id":"b","type":"menu","submenus":[{"id":"a"}]}]})",
        "menu cycle");
    expect_invalid(R"({"modules":[{"id":"same"},{"id":"same"}]})", "duplicate module id");
    expect_invalid(
        R"({"modules":[{"id":"list","type":"GenericList","list_items":[{"title":"Run","action":"/bin/true","timeout":0}]}]})",
        "timeout");
    expect_invalid(R"({"modules":[{"id":"list","type":"GenericList"}]})",
                   "list_items array or items_source");

    if (argc == 3) {
        const auto full_config = LegacyConfig::load(argv[2], &diagnostic);
        assert(full_config.has_value());
        assert(full_config->modules().size() == 55U);
        const auto counts = full_config->counts();
        assert(counts.module_declarations == 55U);
        assert(counts.submenu_references == 59U);
        assert(full_config->find("network_menu") != nullptr);
        assert(full_config->find("fpga_12_3_inch")->type == LegacyModuleType::GenericList);
    }
    return 0;
}
