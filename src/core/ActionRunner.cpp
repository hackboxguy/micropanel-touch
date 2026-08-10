#include "core/ActionRunner.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>

namespace micropanel_touch::core {
namespace {

constexpr std::string_view kErrorMarkers[] = {
    "[ERROR]",
    "Error",
    "Failed",
    "failed",
};

constexpr std::string_view kSuccessMarkers[] = {
    "[SUCCESS]",
    "Flash verification successful",
    "Optionbyte verification successful",
};

bool contains_any(const std::string& text, const std::string_view* markers,
                  std::size_t marker_count) {
    return std::any_of(markers, markers + marker_count, [&text](std::string_view marker) {
        return text.find(marker) != std::string::npos;
    });
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    return std::string(first, last.base());
}

std::string result_text_for(const ActionDefinition& definition, const std::string& log) {
    if (definition.result_pattern.empty()) {
        return {};
    }

    std::optional<std::string> matched_text;
    std::size_t offset = 0U;
    while (offset <= log.size()) {
        const std::size_t end = log.find('\n', offset);
        const std::string_view line(log.data() + offset,
                                    (end == std::string::npos ? log.size() : end) - offset);
        const std::size_t match = line.find(definition.result_pattern);
        if (match != std::string_view::npos) {
            matched_text = trim(std::string(line.substr(match + definition.result_pattern.size())));
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1U;
    }

    if (!matched_text.has_value()) {
        return {};
    }
    return definition.result_prefix + *matched_text;
}

std::optional<unsigned int> progress_for(const ActionDefinition& definition,
                                         const std::string& log,
                                         std::chrono::seconds elapsed) {
    if (definition.parse_progress) {
        if (const auto parsed = ActionRunner::parse_progress_percent(log); parsed.has_value()) {
            return parsed;
        }
    }
    if (definition.usb_blaster_duration_seconds.has_value()) {
        return ActionRunner::estimated_progress(elapsed, *definition.usb_blaster_duration_seconds,
                                                true);
    }
    return std::nullopt;
}

ActionResult terminal_result(ActionResultStatus status, const CommandCompletion& completion,
                             const std::optional<std::string>& configured_log) {
    ActionResult result;
    result.status = status;
    result.exit_status = completion.exit_status;
    if (configured_log.has_value()) {
        result.log_tail = ActionRunner::last_log_lines(*configured_log);
    } else {
        result.log_tail = ActionRunner::last_log_lines(completion.output);
    }
    return result;
}

}  // namespace

ActionDefinition ActionDefinition::from_legacy(const LegacyListItem& item) {
    return {
        item.log_file,
        item.result_pattern,
        item.result_prefix,
        item.usb_blaster_duration_seconds,
        item.parse_progress,
    };
}

ActionResult ActionRunner::evaluate(const ActionDefinition& definition,
                                    const CommandCompletion& completion,
                                    const std::optional<std::string>& configured_log,
                                    std::chrono::seconds elapsed) {
    switch (completion.status) {
        case CommandCompletionStatus::StartFailed:
            return terminal_result(ActionResultStatus::StartFailed, completion, configured_log);
        case CommandCompletionStatus::Cancelled:
            return terminal_result(ActionResultStatus::Cancelled, completion, configured_log);
        case CommandCompletionStatus::TimedOut:
            return terminal_result(ActionResultStatus::TimedOut, completion, configured_log);
        case CommandCompletionStatus::OutputLimitExceeded:
            return terminal_result(ActionResultStatus::Failed, completion, configured_log);
        case CommandCompletionStatus::Succeeded:
        case CommandCompletionStatus::Failed:
            break;
    }

    if (definition.log_file.empty()) {
        return terminal_result(ActionResultStatus::AssumedSucceeded, completion, configured_log);
    }
    if (!configured_log.has_value()) {
        return terminal_result(ActionResultStatus::Failed, completion, configured_log);
    }

    ActionResult result;
    result.exit_status = completion.exit_status;
    result.log_tail = last_log_lines(*configured_log);
    result.result_text = result_text_for(definition, *configured_log);
    result.progress_percent = progress_for(definition, *configured_log, elapsed);
    result.progress_is_estimated = definition.usb_blaster_duration_seconds.has_value() &&
                                   (!definition.parse_progress ||
                                    !parse_progress_percent(*configured_log).has_value());

    if (contains_any(*configured_log, kErrorMarkers,
                     sizeof(kErrorMarkers) / sizeof(kErrorMarkers[0]))) {
        result.status = ActionResultStatus::Failed;
    } else if (contains_any(*configured_log, kSuccessMarkers,
                            sizeof(kSuccessMarkers) / sizeof(kSuccessMarkers[0]))) {
        result.status = ActionResultStatus::Succeeded;
    } else {
        result.status = completion.exit_status == 0 ? ActionResultStatus::Succeeded
                                                    : ActionResultStatus::Failed;
    }
    return result;
}

std::optional<unsigned int> ActionRunner::parse_progress_percent(const std::string& log) {
    std::optional<unsigned int> latest;
    for (std::size_t percent = 0U; percent < log.size(); ++percent) {
        if (log[percent] != '%') {
            continue;
        }
        std::size_t begin = percent;
        while (begin > 0U && std::isdigit(static_cast<unsigned char>(log[begin - 1U])) != 0) {
            --begin;
        }
        if (begin == percent) {
            continue;
        }

        bool negative = false;
        if (begin > 0U && log[begin - 1U] == '-') {
            negative = true;
        }

        unsigned int value = 0U;
        for (std::size_t index = begin; index < percent; ++index) {
            const unsigned int digit = static_cast<unsigned int>(log[index] - '0');
            if (value > (std::numeric_limits<unsigned int>::max() - digit) / 10U) {
                value = std::numeric_limits<unsigned int>::max();
                break;
            }
            value = value * 10U + digit;
        }
        latest = negative ? 0U : std::min(value, 100U);
    }
    return latest;
}

unsigned int ActionRunner::estimated_progress(std::chrono::seconds elapsed,
                                               std::uint32_t duration_seconds, bool terminal) {
    if (terminal) {
        return 100U;
    }
    if (duration_seconds == 0U || elapsed.count() <= 0) {
        return 0U;
    }
    const auto elapsed_seconds = static_cast<std::uint64_t>(elapsed.count());
    const std::uint64_t percentage = elapsed_seconds * 100U / duration_seconds;
    return static_cast<unsigned int>(std::min<std::uint64_t>(percentage, 99U));
}

std::vector<std::string> ActionRunner::last_log_lines(const std::string& log,
                                                       std::size_t maximum_lines) {
    if (maximum_lines == 0U) {
        return {};
    }

    std::vector<std::string> lines;
    std::size_t offset = 0U;
    while (offset <= log.size()) {
        const std::size_t end = log.find('\n', offset);
        const std::string line = std::string(log.data() + offset,
                                             (end == std::string::npos ? log.size() : end) - offset);
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1U;
    }
    if (lines.size() > maximum_lines) {
        lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(maximum_lines));
    }
    return lines;
}

}  // namespace micropanel_touch::core
