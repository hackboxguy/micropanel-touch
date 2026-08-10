#pragma once

#include "core/LegacyConfig.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace micropanel_touch::core {

struct CommandCompletion;

/**
 * Execution metadata retained from a legacy GenericList item.
 *
 * This deliberately does not contain an executable or raw action string.
 * Command compilation and CommandService ownership are the next ActionRunner
 * integration step; this component owns only renderer-independent result and
 * progress semantics so they can be locked down with fixture tests first.
 */
struct ActionDefinition {
    std::string log_file;
    std::string result_pattern;
    std::string result_prefix;
    std::optional<std::uint32_t> usb_blaster_duration_seconds;
    bool parse_progress{false};

    static ActionDefinition from_legacy(const LegacyListItem& item);
};

enum class ActionResultStatus {
    StartFailed,
    Cancelled,
    TimedOut,
    Killed,
    Failed,
    Succeeded,
    AssumedSucceeded,
};

struct ActionProgress {
    std::optional<unsigned int> progress_percent;
    bool progress_is_estimated{false};
    std::vector<std::string> log_tail;
};

/**
 * A terminal action outcome for the progress/result renderer. `log_tail` is
 * bounded to the last three non-empty log lines and is safe to display as
 * ordinary text. Sensitive actions must not provide sensitive text here.
 */
struct ActionResult {
    ActionResultStatus status{ActionResultStatus::StartFailed};
    int exit_status{-1};
    int terminating_signal{0};
    std::optional<unsigned int> progress_percent;
    bool progress_is_estimated{false};
    std::string result_text;
    // A renderer-facing explanation that is not obtained from action output.
    // It is reserved for operational failures such as a managed log write.
    std::string diagnostic;
    std::vector<std::string> log_tail;
};

/**
 * Display-agnostic compatibility evaluator for legacy action output.
 *
 * The eventual runtime owner will obtain `configured_log` from its one
 * managed log writer and pass it here with the CommandService terminal event.
 * Keeping file I/O out of this class makes all legacy precedence rules
 * deterministic and testable without a display or a child process.
 */
class ActionRunner {
public:
    static ActionResult evaluate(const ActionDefinition& definition,
                                 const CommandCompletion& completion,
                                 const std::optional<std::string>& configured_log,
                                 std::chrono::seconds elapsed = std::chrono::seconds::zero());

    // Used while an action is live. Estimated progress deliberately caps at
    // 99%; callers pass terminal=true only for a completed result card.
    static ActionProgress progress(const ActionDefinition& definition, const std::string& log,
                                   std::chrono::seconds elapsed, bool terminal = false);
    static std::optional<unsigned int> parse_progress_percent(const std::string& log);
    static unsigned int estimated_progress(std::chrono::seconds elapsed,
                                           std::uint32_t duration_seconds,
                                           bool terminal);
    static std::vector<std::string> last_log_lines(const std::string& log,
                                                   std::size_t maximum_lines = 3U);
};

}  // namespace micropanel_touch::core
