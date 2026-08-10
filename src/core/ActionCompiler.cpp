#include "core/ActionCompiler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace micropanel_touch::core {
namespace {

bool has_nul(std::string_view text) {
    return text.find('\0') != std::string_view::npos;
}

bool has_dollar(std::string_view text) {
    return text.find('$') != std::string_view::npos;
}

bool is_within(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    const std::filesystem::path normal_candidate = candidate.lexically_normal();
    const std::filesystem::path normal_root = root.lexically_normal();
    auto candidate_part = normal_candidate.begin();
    for (auto root_part = normal_root.begin(); root_part != normal_root.end(); ++root_part) {
        if (candidate_part == normal_candidate.end() || *candidate_part != *root_part) {
            return false;
        }
        ++candidate_part;
    }
    return true;
}

bool validate_root(const std::filesystem::path& path, const char* name, std::string* diagnostic) {
    if (path.empty() || !path.is_absolute() || has_nul(path.string()) || has_dollar(path.string()) ||
        path.lexically_normal() != path) {
        if (diagnostic != nullptr) {
            *diagnostic = std::string("ExecutionContext has invalid ") + name;
        }
        return false;
    }
    return true;
}

bool path_boundary_before(std::string_view text, std::size_t offset) {
    if (offset == 0U) {
        return true;
    }
    const unsigned char previous = static_cast<unsigned char>(text[offset - 1U]);
    return std::isspace(previous) != 0 || previous == '\'' || previous == '"' || previous == '=' ||
           previous == ':' || previous == '(';
}

bool path_boundary_after(std::string_view text, std::size_t offset) {
    return offset == text.size() || text[offset] == '/' || std::isspace(static_cast<unsigned char>(text[offset])) != 0 ||
           text[offset] == '\'' || text[offset] == '"' || text[offset] == ')' || text[offset] == ';';
}

void replace_legacy_path(std::string* text, std::string_view legacy, const std::string& replacement) {
    std::size_t offset = 0U;
    while ((offset = text->find(legacy, offset)) != std::string::npos) {
        const std::size_t after = offset + legacy.size();
        if (path_boundary_before(*text, offset) && path_boundary_after(*text, after)) {
            text->replace(offset, legacy.size(), replacement);
            offset += replacement.size();
        } else {
            offset = after;
        }
    }
}

bool is_token_start(unsigned char character) {
    return std::isalpha(character) != 0 || character == '_';
}

bool is_token_character(unsigned char character) {
    return std::isalnum(character) != 0 || character == '_';
}

bool verify_expanded_roots(const std::string& text, const ExecutionContext& context,
                           std::string* diagnostic) {
    const std::array<std::filesystem::path, 3> roots{
        context.micropanel_home,
        context.data_dir,
        context.log_dir,
    };
    for (const auto& root : roots) {
        const std::string root_text = root.string();
        std::size_t offset = 0U;
        while ((offset = text.find(root_text, offset)) != std::string::npos) {
            std::size_t end = offset + root_text.size();
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
                   text[end] != '\'' && text[end] != '"' && text[end] != ';' && text[end] != '|' &&
                   text[end] != '&' && text[end] != ')') {
                ++end;
            }
            const std::string suffix = text.substr(offset + root_text.size(), end - offset - root_text.size());
            if (!suffix.empty() && suffix.front() == '/') {
                const std::filesystem::path candidate = root / suffix.substr(1U);
                if (!is_within(candidate, root)) {
                    if (diagnostic != nullptr) {
                        *diagnostic = "Expanded execution field escapes an approved root";
                    }
                    return false;
                }
            }
            offset = end;
        }
    }
    return true;
}

bool valid_log_basename(std::string_view filename) {
    return !filename.empty() && filename != "." && filename != ".." &&
           filename.find('/') == std::string_view::npos && filename.find('\\') == std::string_view::npos &&
           !has_nul(filename);
}

std::optional<VettedAction> fail(std::string_view message, std::string* diagnostic) {
    if (diagnostic != nullptr) {
        *diagnostic = message;
    }
    return std::nullopt;
}

}  // namespace

bool ExecutionContext::validate(std::string* diagnostic) const {
    if (!validate_root(micropanel_home, "micropanel_home", diagnostic) ||
        !validate_root(config_dir, "config_dir", diagnostic) ||
        !validate_root(data_dir, "data_dir", diagnostic) ||
        !validate_root(log_dir, "log_dir", diagnostic) ||
        !validate_root(runtime_dir, "runtime_dir", diagnostic)) {
        return false;
    }
    if (!is_within(config_dir, micropanel_home) || !is_within(log_dir, data_dir)) {
        if (diagnostic != nullptr) {
            *diagnostic = "ExecutionContext contains a root outside its approved parent";
        }
        return false;
    }
    return true;
}

VettedAction::VettedAction(ActionDefinition definition, std::string executable,
                           std::vector<std::string> arguments, std::chrono::milliseconds timeout,
                           std::size_t maximum_output_bytes,
                           std::chrono::milliseconds termination_grace,
                           std::optional<std::filesystem::path> managed_log_path)
    : definition_(std::move(definition)), executable_(std::move(executable)),
      arguments_(std::move(arguments)), timeout_(timeout), maximum_output_bytes_(maximum_output_bytes),
      termination_grace_(termination_grace), managed_log_path_(std::move(managed_log_path)) {}

const ActionDefinition& VettedAction::definition() const { return definition_; }
const std::string& VettedAction::executable() const { return executable_; }
const std::vector<std::string>& VettedAction::arguments() const { return arguments_; }
std::chrono::milliseconds VettedAction::timeout() const { return timeout_; }
std::size_t VettedAction::maximum_output_bytes() const { return maximum_output_bytes_; }
std::chrono::milliseconds VettedAction::termination_grace() const { return termination_grace_; }
const std::optional<std::filesystem::path>& VettedAction::managed_log_path() const {
    return managed_log_path_;
}

std::optional<std::string> ActionCompiler::expand_execution_field(
    std::string_view source, const ExecutionContext& context, std::string* diagnostic) {
    if (!context.validate(diagnostic)) {
        return std::nullopt;
    }
    if (has_nul(source)) {
        if (diagnostic != nullptr) {
            *diagnostic = "Execution field contains a NUL byte";
        }
        return std::nullopt;
    }

    std::string rewritten(source);
    // Exact mapping wins before the containing legacy home prefix.
    replace_legacy_path(&rewritten, "/home/pi/micropanel/settings.json",
                        (context.data_dir / "settings.json").string());
    replace_legacy_path(&rewritten, "/home/pi/micropanel", context.micropanel_home.string());

    std::string expanded;
    expanded.reserve(rewritten.size());
    for (std::size_t index = 0U; index < rewritten.size();) {
        if (rewritten[index] != '$') {
            expanded.push_back(rewritten[index++]);
            continue;
        }
        if (index + 1U >= rewritten.size() || !is_token_start(
                static_cast<unsigned char>(rewritten[index + 1U]))) {
            if (diagnostic != nullptr) {
                *diagnostic = "Execution field has an unsupported runtime or shell token";
            }
            return std::nullopt;
        }
        std::size_t end = index + 1U;
        while (end < rewritten.size() &&
               is_token_character(static_cast<unsigned char>(rewritten[end]))) {
            ++end;
        }
        const std::string_view token(rewritten.data() + index, end - index);
        if (token == "$MICROPANEL_HOME") {
            expanded += context.micropanel_home.string();
        } else if (token == "$MICROPANEL_DATA") {
            expanded += context.data_dir.string();
        } else if (token == "$MICROPANEL_LOG") {
            expanded += context.log_dir.string();
        } else {
            if (diagnostic != nullptr) {
                *diagnostic = "Execution field contains unknown token " + std::string(token);
            }
            return std::nullopt;
        }
        index = end;
    }
    if (!verify_expanded_roots(expanded, context, diagnostic)) {
        return std::nullopt;
    }
    return expanded;
}

std::optional<std::filesystem::path> ActionCompiler::resolve_legacy_log_file(
    std::string_view source, const ExecutionContext& context, std::string* diagnostic) {
    if (source.empty()) {
        return std::nullopt;
    }
    const auto expanded = expand_execution_field(source, context, diagnostic);
    if (!expanded.has_value()) {
        return std::nullopt;
    }
    constexpr std::string_view kLegacyTmpPrefix = "/tmp/";
    if (expanded->rfind(kLegacyTmpPrefix, 0) != 0U) {
        if (diagnostic != nullptr) {
            *diagnostic = "Legacy action log must be a /tmp basename";
        }
        return std::nullopt;
    }
    const std::string_view basename(expanded->data() + kLegacyTmpPrefix.size(),
                                    expanded->size() - kLegacyTmpPrefix.size());
    if (!valid_log_basename(basename)) {
        if (diagnostic != nullptr) {
            *diagnostic = "Legacy action log has an invalid basename";
        }
        return std::nullopt;
    }
    const std::filesystem::path resolved = context.log_dir / std::string(basename);
    if (!is_within(resolved, context.log_dir)) {
        if (diagnostic != nullptr) {
            *diagnostic = "Legacy action log escapes log_dir";
        }
        return std::nullopt;
    }
    return resolved;
}

std::optional<VettedAction> ActionCompiler::compile_native(std::string_view action_id,
                                                            const ExecutionContext& context,
                                                            std::string* diagnostic) {
    if (!context.validate(diagnostic)) {
        return std::nullopt;
    }
    if (action_id != "demo.simulated-flash") {
        return fail("Native action is not allowlisted", diagnostic);
    }

    ActionDefinition definition;
    definition.log_file = "simulated-flash.log";
    definition.parse_progress = true;
    const std::filesystem::path log_path = context.log_dir / definition.log_file;
    return VettedAction(
        definition, "/bin/sh",
        {"-c", "printf '%s\\n' 'Progress: 0%'; sleep 1; "
               "printf '%s\\n' 'Progress: 20%'; sleep 1; "
               "printf '%s\\n' 'Progress: 40%'; sleep 1; "
               "printf '%s\\n' 'Progress: 60%'; sleep 1; "
               "printf '%s\\n' 'Progress: 80%'; sleep 1; "
               "printf '%s\\n' 'Progress: 100%' '[SUCCESS] simulated flash complete'"},
        std::chrono::seconds(12), 4096U, std::chrono::milliseconds(1500), log_path);
}

std::optional<VettedAction> ActionCompiler::compile_legacy(const LegacyListItem& item,
                                                            const ExecutionContext& context,
                                                            std::string* diagnostic) {
    if (!context.validate(diagnostic)) {
        return std::nullopt;
    }
    if (item.action.empty()) {
        return fail("Legacy list item has no action", diagnostic);
    }
    return fail("Legacy action requires an allowlisted compatibility template", diagnostic);
}

}  // namespace micropanel_touch::core
