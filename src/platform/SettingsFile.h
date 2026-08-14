#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>

namespace micropanel_touch::platform {

// All appliance settings files use a deliberately small key=value grammar.
// Keep file-opening, bounded reads, duplicate-key rejection, and the durable
// replace protocol in one place so a later settings format cannot quietly
// regress its persistence or symlink-safety posture.
enum class SettingsFileError {
    None,
    Missing,
    Open,
    Metadata,
    Read,
    InvalidLine,
    UnknownOrRepeatedKey,
    Incomplete,
    Create,
    Write,
    Replace,
    OpenParent,
    Sync,
};

using SettingsFileValues = std::map<std::string, std::string>;

std::optional<SettingsFileValues> load_settings_file(
    const std::filesystem::path& path, std::size_t maximum_bytes,
    const std::string_view* allowed_keys, std::size_t allowed_key_count,
    mode_t allowed_permissions, SettingsFileError* error = nullptr);

bool save_settings_file(const std::filesystem::path& path, std::string_view content,
                        mode_t permissions, SettingsFileError* error = nullptr);

}  // namespace micropanel_touch::platform
