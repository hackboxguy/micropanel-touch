#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/ActionRunner.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using micropanel_touch::core::ActionDefinition;
using micropanel_touch::core::ActionResultStatus;
using micropanel_touch::core::ActionRunner;
using micropanel_touch::core::CommandCompletion;
using micropanel_touch::core::CommandCompletionStatus;

namespace {

std::string read_fixture(const std::filesystem::path& fixture_directory, const char* name) {
    std::ifstream stream(fixture_directory / name);
    assert(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

CommandCompletion completion(CommandCompletionStatus status, int exit_status) {
    return {42U, status, exit_status, {}};
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path fixture_directory(argv[1]);
    ActionDefinition logged_action;
    logged_action.log_file = "action.log";

    const auto contradictory = ActionRunner::evaluate(
        logged_action, completion(CommandCompletionStatus::Succeeded, 0),
        read_fixture(fixture_directory, "fpga-contradictory.log"));
    assert(contradictory.status == ActionResultStatus::Failed);
    assert(contradictory.log_tail.size() == 3U);
    assert(contradictory.log_tail.back() == "[ERROR] Verification checksum mismatch");

    const auto success_with_nonzero = ActionRunner::evaluate(
        logged_action, completion(CommandCompletionStatus::Failed, 8),
        read_fixture(fixture_directory, "fpga-success-nonzero.log"));
    assert(success_with_nonzero.status == ActionResultStatus::Succeeded);
    assert(success_with_nonzero.exit_status == 8);

    const std::string markerless = read_fixture(fixture_directory, "generic-markerless.log");
    assert(ActionRunner::evaluate(logged_action, completion(CommandCompletionStatus::Succeeded, 0),
                                  markerless)
               .status == ActionResultStatus::Succeeded);
    assert(ActionRunner::evaluate(logged_action, completion(CommandCompletionStatus::Failed, 7),
                                  markerless)
               .status == ActionResultStatus::Failed);
    assert(ActionRunner::evaluate(logged_action, completion(CommandCompletionStatus::Succeeded, 0),
                                  std::nullopt)
               .status == ActionResultStatus::Failed);

    const ActionDefinition no_log_action;
    assert(ActionRunner::evaluate(no_log_action, completion(CommandCompletionStatus::Failed, 9),
                                  std::nullopt)
               .status == ActionResultStatus::AssumedSucceeded);

    assert(ActionRunner::evaluate(logged_action,
                                  completion(CommandCompletionStatus::StartFailed, 127),
                                  std::nullopt)
               .status == ActionResultStatus::StartFailed);
    assert(ActionRunner::evaluate(logged_action,
                                  completion(CommandCompletionStatus::Cancelled, -1), markerless)
               .status == ActionResultStatus::Cancelled);
    assert(ActionRunner::evaluate(logged_action,
                                  completion(CommandCompletionStatus::TimedOut, -1), markerless)
               .status == ActionResultStatus::TimedOut);
    assert(ActionRunner::evaluate(logged_action,
                                  completion(CommandCompletionStatus::OutputLimitExceeded, -1),
                                  markerless)
               .status == ActionResultStatus::Failed);

    ActionDefinition rh850_action;
    rh850_action.log_file = "rh850.log";
    rh850_action.parse_progress = true;
    rh850_action.result_pattern = "Result:";
    rh850_action.result_prefix = "Unit ";
    const auto rh850 = ActionRunner::evaluate(
        rh850_action, completion(CommandCompletionStatus::Succeeded, 0),
        read_fixture(fixture_directory, "rh850-progress-result.log"));
    assert(rh850.status == ActionResultStatus::Succeeded);
    assert(rh850.progress_percent.has_value());
    assert(*rh850.progress_percent == 100U);
    assert(!rh850.progress_is_estimated);
    assert(rh850.result_text == "Unit final id");
    assert(ActionRunner::parse_progress_percent("nothing useful") == std::nullopt);

    assert(ActionRunner::estimated_progress(std::chrono::seconds(10), 20U, false) == 50U);
    assert(ActionRunner::estimated_progress(std::chrono::seconds(99), 20U, false) == 99U);
    assert(ActionRunner::estimated_progress(std::chrono::seconds(99), 20U, true) == 100U);

    ActionDefinition legacy_definition = ActionDefinition::from_legacy(
        {"Flash", "fixed-action", true, 30U, "legacy.log", "Flashing", "Result:", "ID ",
         15U, true});
    assert(legacy_definition.log_file == "legacy.log");
    assert(legacy_definition.parse_progress);
    assert(legacy_definition.usb_blaster_duration_seconds == 15U);
    return 0;
}
