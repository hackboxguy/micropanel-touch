#include "platform/SettingsFile.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

void set_error(SettingsFileError* error, SettingsFileError value) {
    if (error != nullptr) {
        *error = value;
    }
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

bool is_allowed_key(std::string_view key, const std::string_view* allowed_keys,
                    std::size_t allowed_key_count) {
    return std::find(allowed_keys, allowed_keys + allowed_key_count, key) !=
           allowed_keys + allowed_key_count;
}

}  // namespace

std::optional<SettingsFileValues> load_settings_file(
    const fs::path& path, std::size_t maximum_bytes, const std::string_view* allowed_keys,
    std::size_t allowed_key_count, mode_t allowed_permissions, SettingsFileError* error) {
    set_error(error, SettingsFileError::None);
    if (path.empty() || maximum_bytes == 0U || allowed_keys == nullptr || allowed_key_count == 0U) {
        set_error(error, SettingsFileError::Metadata);
        return std::nullopt;
    }

    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        set_error(error, errno == ENOENT ? SettingsFileError::Missing : SettingsFileError::Open);
        return std::nullopt;
    }
    struct stat metadata {};
    if (::fstat(fd, &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_size < 0 ||
        static_cast<std::uintmax_t>(metadata.st_size) > maximum_bytes ||
        (((metadata.st_mode & 0777) & ~allowed_permissions) != 0)) {
        ::close(fd);
        set_error(error, SettingsFileError::Metadata);
        return std::nullopt;
    }

    std::string content;
    content.reserve(static_cast<std::size_t>(metadata.st_size));
    std::array<char, 128U> buffer{};
    for (;;) {
        const ssize_t read_count = ::read(fd, buffer.data(), buffer.size());
        if (read_count < 0) {
            ::close(fd);
            set_error(error, SettingsFileError::Read);
            return std::nullopt;
        }
        if (read_count == 0) {
            break;
        }
        content.append(buffer.data(), static_cast<std::size_t>(read_count));
        if (content.size() > maximum_bytes) {
            ::close(fd);
            set_error(error, SettingsFileError::Metadata);
            return std::nullopt;
        }
    }
    if (::close(fd) != 0) {
        set_error(error, SettingsFileError::Read);
        return std::nullopt;
    }

    SettingsFileValues values;
    std::size_t line_start = 0U;
    while (line_start < content.size()) {
        const std::size_t line_end = content.find('\n', line_start);
        const std::size_t line_size = (line_end == std::string::npos ? content.size() : line_end) - line_start;
        const std::string_view line(content.data() + line_start, line_size);
        const std::size_t delimiter = line.find('=');
        if (delimiter == std::string_view::npos || delimiter == 0U || delimiter + 1U == line.size()) {
            set_error(error, SettingsFileError::InvalidLine);
            return std::nullopt;
        }
        const std::string_view key = line.substr(0U, delimiter);
        if (!is_allowed_key(key, allowed_keys, allowed_key_count) ||
            values.count(std::string(key)) != 0U) {
            set_error(error, SettingsFileError::UnknownOrRepeatedKey);
            return std::nullopt;
        }
        values.emplace(std::string(key), std::string(line.substr(delimiter + 1U)));
        if (line_end == std::string::npos) {
            break;
        }
        line_start = line_end + 1U;
    }
    if (values.size() != allowed_key_count) {
        set_error(error, SettingsFileError::Incomplete);
        return std::nullopt;
    }
    return values;
}

bool save_settings_file(const fs::path& path, std::string_view content, mode_t permissions,
                        SettingsFileError* error) {
    set_error(error, SettingsFileError::None);
    if (path.empty() || path.parent_path().empty()) {
        set_error(error, SettingsFileError::Create);
        return false;
    }
    const fs::path temporary = path.string() + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                          permissions);
    if (fd < 0) {
        set_error(error, SettingsFileError::Create);
        return false;
    }
    const bool written = ::fchmod(fd, permissions) == 0 && write_all(fd, content) && ::fsync(fd) == 0;
    const int close_status = ::close(fd);
    if (!written || close_status != 0) {
        ::unlink(temporary.c_str());
        set_error(error, SettingsFileError::Write);
        return false;
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        set_error(error, SettingsFileError::Replace);
        return false;
    }
    const int parent_fd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd < 0) {
        set_error(error, SettingsFileError::OpenParent);
        return false;
    }
    const int sync_status = ::fsync(parent_fd);
    const int close_parent_status = ::close(parent_fd);
    if (sync_status != 0 || close_parent_status != 0) {
        set_error(error, SettingsFileError::Sync);
        return false;
    }
    return true;
}

}  // namespace micropanel_touch::platform
