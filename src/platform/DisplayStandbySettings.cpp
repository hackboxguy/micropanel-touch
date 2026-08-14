#include "platform/DisplayStandbySettings.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

constexpr int kFormatVersion = 1;
constexpr std::size_t kMaximumFileBytes = 128U;
constexpr std::array<std::string_view, 3> kKeys{"version", "enabled", "seconds"};

void set_diagnostic(std::string* diagnostic, std::string message) {
    if (diagnostic != nullptr) {
        *diagnostic = std::move(message);
    }
}

bool parse_integer(std::string_view text, int* value) {
    if (text.empty()) {
        return false;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), *value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool write_all(int fd, std::string_view content) {
    std::size_t offset = 0U;
    while (offset < content.size()) {
        const ssize_t written = ::write(fd, content.data() + offset, content.size() - offset);
        if (written <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

}  // namespace

bool display_standby_settings_are_valid(const DisplayStandbySettings& settings) {
    return settings.seconds >= kDisplayStandbyMinimumSeconds &&
           settings.seconds <= kDisplayStandbyMaximumSeconds &&
           settings.seconds % kDisplayStandbyStepSeconds == 0U;
}

std::optional<DisplayStandbySettings> load_display_standby_settings(
    const fs::path& path, std::string* diagnostic) {
    std::error_code error;
    if (!fs::exists(path, error)) {
        if (error) {
            set_diagnostic(diagnostic, "unable to inspect display standby settings: " + error.message());
        }
        return std::nullopt;
    }
    const auto size = fs::file_size(path, error);
    if (error || size > kMaximumFileBytes) {
        set_diagnostic(diagnostic, "display standby settings file is invalid");
        return std::nullopt;
    }
    std::ifstream input(path);
    if (!input) {
        set_diagnostic(diagnostic, "unable to read display standby settings");
        return std::nullopt;
    }
    std::map<std::string, int> values;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t delimiter = line.find('=');
        if (delimiter == std::string::npos || delimiter == 0U || delimiter + 1U == line.size()) {
            set_diagnostic(diagnostic, "display standby settings file contains an invalid line");
            return std::nullopt;
        }
        const std::string key = line.substr(0U, delimiter);
        if (std::find(kKeys.begin(), kKeys.end(), key) == kKeys.end() || values.count(key) != 0U) {
            set_diagnostic(diagnostic,
                           "display standby settings file contains an unknown or repeated key");
            return std::nullopt;
        }
        int value = 0;
        if (!parse_integer(std::string_view(line).substr(delimiter + 1U), &value)) {
            set_diagnostic(diagnostic, "display standby settings file contains a non-integer value");
            return std::nullopt;
        }
        values.emplace(key, value);
    }
    if (values.size() != kKeys.size() || values["version"] != kFormatVersion ||
        (values["enabled"] != 0 && values["enabled"] != 1) || values["seconds"] < 0) {
        set_diagnostic(diagnostic, "display standby settings file is unsupported");
        return std::nullopt;
    }
    const DisplayStandbySettings settings{values["enabled"] == 1,
                                          static_cast<unsigned int>(values["seconds"])};
    if (!display_standby_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "display standby settings are outside the supported range");
        return std::nullopt;
    }
    return settings;
}

bool save_display_standby_settings(const fs::path& path, const DisplayStandbySettings& settings,
                                   std::string* diagnostic) {
    if (path.empty() || path.parent_path().empty() || !display_standby_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "display standby settings are invalid");
        return false;
    }
    const std::string content = "version=" + std::to_string(kFormatVersion) + "\n" +
                                "enabled=" + std::to_string(settings.enabled ? 1 : 0) + "\n" +
                                "seconds=" + std::to_string(settings.seconds) + "\n";
    const fs::path temporary = path.string() + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                          0640);
    if (fd < 0) {
        set_diagnostic(diagnostic,
                       "unable to create display standby settings: " + std::string(std::strerror(errno)));
        return false;
    }
    const bool written = write_all(fd, content) && ::fsync(fd) == 0;
    const int close_status = ::close(fd);
    if (!written || close_status != 0) {
        ::unlink(temporary.c_str());
        set_diagnostic(diagnostic, "unable to write display standby settings");
        return false;
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        set_diagnostic(diagnostic,
                       "unable to replace display standby settings: " + std::string(std::strerror(errno)));
        return false;
    }
    const int parent_fd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd < 0) {
        set_diagnostic(diagnostic,
                       "unable to sync display standby settings directory: " +
                           std::string(std::strerror(errno)));
        return false;
    }
    const int sync_status = ::fsync(parent_fd);
    const int parent_close_status = ::close(parent_fd);
    if (sync_status != 0 || parent_close_status != 0) {
        set_diagnostic(diagnostic, "unable to sync display standby settings directory");
        return false;
    }
    return true;
}

}  // namespace micropanel_touch::platform
