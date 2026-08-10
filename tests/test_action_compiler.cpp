#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/ActionCompiler.h"

#include <cassert>
#include <filesystem>
#include <string>

using micropanel_touch::core::ActionCompiler;
using micropanel_touch::core::ExecutionContext;
using micropanel_touch::core::LegacyListItem;

namespace {

ExecutionContext test_context() {
    return {
        "/opt/micropanel-touch",
        "/opt/micropanel-touch/share/micropanel-touch/screens",
        "/var/lib/micropanel-touch",
        "/var/lib/micropanel-touch/logs",
        "/run/micropanel-touch",
    };
}

}  // namespace

int main() {
    const ExecutionContext context = test_context();
    std::string diagnostic;
    assert(context.validate(&diagnostic));

    const auto expanded = ActionCompiler::expand_execution_field(
        "$MICROPANEL_HOME/bin/tool --data=$MICROPANEL_DATA/state --log=$MICROPANEL_LOG/action.log",
        context, &diagnostic);
    assert(expanded.has_value());
    assert(*expanded == "/opt/micropanel-touch/bin/tool --data=/var/lib/micropanel-touch/state "
                        "--log=/var/lib/micropanel-touch/logs/action.log");

    const auto remapped = ActionCompiler::expand_execution_field(
        "/home/pi/micropanel/settings.json /home/pi/micropanel/scripts/tool", context, &diagnostic);
    assert(remapped.has_value());
    assert(*remapped == "/var/lib/micropanel-touch/settings.json "
                        "/opt/micropanel-touch/scripts/tool");
    const auto boundary_preserved = ActionCompiler::expand_execution_field(
        "/home/pi/micropanelish/tool", context, &diagnostic);
    assert(boundary_preserved.has_value());
    assert(*boundary_preserved == "/home/pi/micropanelish/tool");

    assert(!ActionCompiler::expand_execution_field("$UNKNOWN/tool", context, &diagnostic));
    assert(!ActionCompiler::expand_execution_field("$MICROPANEL_HOME/../escape", context, &diagnostic));
    assert(!ActionCompiler::expand_execution_field("$1", context, &diagnostic));
    const std::string nul_field{"safe\0unsafe", 11U};
    assert(!ActionCompiler::expand_execution_field(nul_field, context, &diagnostic));

    const auto log_file =
        ActionCompiler::resolve_legacy_log_file("/tmp/fpga-flash.log", context, &diagnostic);
    assert(log_file.has_value());
    assert(*log_file == "/var/lib/micropanel-touch/logs/fpga-flash.log");
    assert(!ActionCompiler::resolve_legacy_log_file("/tmp/nested/fpga.log", context, &diagnostic));
    assert(!ActionCompiler::resolve_legacy_log_file("/var/tmp/fpga.log", context, &diagnostic));

    auto demo = ActionCompiler::compile_native("demo.simulated-flash", context, &diagnostic);
    assert(demo.has_value());
    assert(demo->executable() == "/bin/sh");
    assert(demo->arguments().size() == 2U);
    assert(demo->definition().parse_progress);
    assert(demo->managed_log_path().has_value());
    assert(*demo->managed_log_path() == "/var/lib/micropanel-touch/logs/simulated-flash.log");
    assert(!ActionCompiler::compile_native("not-allowlisted", context, &diagnostic));

    LegacyListItem raw_legacy{"Unsafe", "sudo /bin/true $1", true, {}, "/tmp/unsafe.log", "", "", "",
                              {}, false};
    const std::string raw_before = raw_legacy.action;
    assert(!ActionCompiler::compile_legacy(raw_legacy, context, &diagnostic));
    assert(raw_legacy.action == raw_before);

    ExecutionContext recursive = context;
    recursive.micropanel_home = "/opt/$MICROPANEL_HOME";
    assert(!recursive.validate(&diagnostic));
    return 0;
}
