#pragma once

#include "core/ActionRunner.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace micropanel_touch::core {

/**
 * Explicit, process-owned execution roots. The caller supplies development
 * roots directly; no ambient environment variable is consulted.
 */
struct ExecutionContext {
    std::filesystem::path micropanel_home;
    std::filesystem::path config_dir;
    std::filesystem::path data_dir;
    std::filesystem::path log_dir;
    std::filesystem::path runtime_dir;
    // Package-owned handlers are the only executable paths native actions may
    // name. Development and installed layouts supply this explicitly.
    std::filesystem::path handler_dir;

    bool validate(std::string* diagnostic) const;
};

/**
 * The only command representation accepted by ActionService. Its constructor
 * is private: a caller cannot convert a legacy string or arbitrary argv into
 * a runnable action without ActionCompiler's allowlist decision.
 */
class VettedAction {
public:
    const ActionDefinition& definition() const;
    const std::string& executable() const;
    const std::vector<std::string>& arguments() const;
    std::chrono::milliseconds timeout() const;
    std::size_t maximum_output_bytes() const;
    std::chrono::milliseconds termination_grace() const;
    const std::optional<std::filesystem::path>& managed_log_path() const;

private:
    VettedAction(ActionDefinition definition, std::string executable,
                 std::vector<std::string> arguments, std::chrono::milliseconds timeout,
                 std::size_t maximum_output_bytes, std::chrono::milliseconds termination_grace,
                 std::optional<std::filesystem::path> managed_log_path);

    ActionDefinition definition_;
    std::string executable_;
    std::vector<std::string> arguments_;
    std::chrono::milliseconds timeout_;
    std::size_t maximum_output_bytes_;
    std::chrono::milliseconds termination_grace_;
    std::optional<std::filesystem::path> managed_log_path_;

    friend class ActionCompiler;
};

/**
 * The initial compiler surface is intentionally small. It validates the
 * execution context and legacy path/token rules, then emits only named,
 * package-owned fixed-argv actions. Raw legacy action strings stay loadable in
 * LegacyConfig but are not executable until a reviewed compatibility adapter
 * is registered here.
 */
class ActionCompiler {
public:
    static std::optional<std::string> expand_execution_field(
        std::string_view source, const ExecutionContext& context, std::string* diagnostic);
    static std::optional<std::filesystem::path> resolve_legacy_log_file(
        std::string_view source, const ExecutionContext& context, std::string* diagnostic);

    static std::optional<VettedAction> compile_native(std::string_view action_id,
                                                       const ExecutionContext& context,
                                                       std::string* diagnostic);
    static std::optional<VettedAction> compile_legacy(const LegacyListItem& item,
                                                       const ExecutionContext& context,
                                                       std::string* diagnostic);
};

}  // namespace micropanel_touch::core
