#include "platform/ScreenLockSettings.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

constexpr int kFormatVersion = 1;
constexpr std::size_t kMaximumFileBytes = 256U;
constexpr std::array<std::string_view, 6> kKeys{
    "version", "enabled", "configured", "iterations", "salt", "verifier"};

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

char hex_digit(unsigned int value) {
    return static_cast<char>(value < 10U ? '0' + value : 'a' + value - 10U);
}

std::string encode_hex(const unsigned char* values, std::size_t count) {
    std::string encoded(count * 2U, '0');
    for (std::size_t index = 0U; index < count; ++index) {
        encoded[2U * index] = hex_digit(values[index] >> 4U);
        encoded[2U * index + 1U] = hex_digit(values[index] & 0x0fU);
    }
    return encoded;
}

std::optional<unsigned char> decode_hex_digit(char character) {
    if (character >= '0' && character <= '9') {
        return static_cast<unsigned char>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<unsigned char>(character - 'a' + 10);
    }
    return std::nullopt;
}

template <std::size_t Count>
bool decode_hex(std::string_view text, std::array<unsigned char, Count>* result) {
    if (text.size() != Count * 2U || result == nullptr) {
        return false;
    }
    for (std::size_t index = 0U; index < Count; ++index) {
        const auto high = decode_hex_digit(text[index * 2U]);
        const auto low = decode_hex_digit(text[index * 2U + 1U]);
        if (!high.has_value() || !low.has_value()) {
            return false;
        }
        (*result)[index] = static_cast<unsigned char>((*high << 4U) | *low);
    }
    return true;
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

bool all_zero(const unsigned char* values, std::size_t count) {
    return std::all_of(values, values + count, [](unsigned char value) { return value == 0U; });
}

}  // namespace

bool screen_lock_pin_is_valid(std::string_view pin) {
    return pin.size() >= kScreenLockPinMinimumDigits &&
           pin.size() <= kScreenLockPinMaximumDigits &&
           std::all_of(pin.begin(), pin.end(), [](char character) {
               return character >= '0' && character <= '9';
           });
}

bool screen_lock_settings_are_valid(const ScreenLockSettings& settings) {
    if (!settings.configured) {
        return !settings.enabled &&
               all_zero(settings.salt.data(), settings.salt.size()) &&
               all_zero(settings.verifier.data(), settings.verifier.size());
    }
    return settings.iterations >= kScreenLockPbkdf2Iterations &&
           !all_zero(settings.salt.data(), settings.salt.size()) &&
           !all_zero(settings.verifier.data(), settings.verifier.size());
}

bool set_screen_lock_pin(ScreenLockSettings* settings, std::string_view pin,
                         std::string* diagnostic) {
    if (settings == nullptr || !screen_lock_pin_is_valid(pin)) {
        set_diagnostic(diagnostic, "PIN must contain 4 to 10 digits");
        return false;
    }
    ScreenLockSettings updated{};
    updated.enabled = false;
    updated.configured = true;
    if (RAND_bytes(updated.salt.data(), static_cast<int>(updated.salt.size())) != 1 ||
        PKCS5_PBKDF2_HMAC(pin.data(), static_cast<int>(pin.size()), updated.salt.data(),
                          static_cast<int>(updated.salt.size()),
                          static_cast<int>(updated.iterations), EVP_sha256(),
                          static_cast<int>(updated.verifier.size()), updated.verifier.data()) != 1) {
        set_diagnostic(diagnostic, "unable to securely process the PIN");
        return false;
    }
    *settings = updated;
    return true;
}

bool verify_screen_lock_pin(const ScreenLockSettings& settings, std::string_view pin) {
    if (!settings.configured || !screen_lock_settings_are_valid(settings) ||
        !screen_lock_pin_is_valid(pin)) {
        return false;
    }
    std::array<unsigned char, kScreenLockVerifierBytes> candidate{};
    const int derived = PKCS5_PBKDF2_HMAC(
        pin.data(), static_cast<int>(pin.size()), settings.salt.data(),
        static_cast<int>(settings.salt.size()), static_cast<int>(settings.iterations), EVP_sha256(),
        static_cast<int>(candidate.size()), candidate.data());
    return derived == 1 && CRYPTO_memcmp(candidate.data(), settings.verifier.data(), candidate.size()) == 0;
}

std::optional<ScreenLockSettings> load_screen_lock_settings(const fs::path& path,
                                                             std::string* diagnostic) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (errno != ENOENT) {
            set_diagnostic(diagnostic,
                           "unable to open screen lock settings: " + std::string(std::strerror(errno)));
        }
        return std::nullopt;
    }
    struct stat metadata {};
    if (::fstat(fd, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        (metadata.st_mode & 0077) != 0 || metadata.st_size < 0 ||
        static_cast<std::uintmax_t>(metadata.st_size) > kMaximumFileBytes) {
        ::close(fd);
        set_diagnostic(diagnostic, "screen lock settings file is invalid");
        return std::nullopt;
    }
    std::string content;
    content.reserve(static_cast<std::size_t>(metadata.st_size));
    std::array<char, 128U> buffer{};
    for (;;) {
        const ssize_t read_count = ::read(fd, buffer.data(), buffer.size());
        if (read_count < 0) {
            ::close(fd);
            set_diagnostic(diagnostic, "unable to read screen lock settings");
            return std::nullopt;
        }
        if (read_count == 0) {
            break;
        }
        content.append(buffer.data(), static_cast<std::size_t>(read_count));
        if (content.size() > kMaximumFileBytes) {
            ::close(fd);
            set_diagnostic(diagnostic, "screen lock settings file is invalid");
            return std::nullopt;
        }
    }
    if (::close(fd) != 0) {
        set_diagnostic(diagnostic, "unable to read screen lock settings");
        return std::nullopt;
    }
    std::istringstream input(content);
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t delimiter = line.find('=');
        if (delimiter == std::string::npos || delimiter == 0U || delimiter + 1U == line.size()) {
            set_diagnostic(diagnostic, "screen lock settings file contains an invalid line");
            return std::nullopt;
        }
        const std::string key = line.substr(0U, delimiter);
        if (std::find(kKeys.begin(), kKeys.end(), key) == kKeys.end() || values.count(key) != 0U) {
            set_diagnostic(diagnostic,
                           "screen lock settings file contains an unknown or repeated key");
            return std::nullopt;
        }
        values.emplace(key, line.substr(delimiter + 1U));
    }
    if (values.size() != kKeys.size()) {
        set_diagnostic(diagnostic, "screen lock settings file is incomplete");
        return std::nullopt;
    }
    int version = 0;
    int enabled = 0;
    int configured = 0;
    int iterations = 0;
    if (!parse_integer(values["version"], &version) || !parse_integer(values["enabled"], &enabled) ||
        !parse_integer(values["configured"], &configured) ||
        !parse_integer(values["iterations"], &iterations) || version != kFormatVersion ||
        (enabled != 0 && enabled != 1) || (configured != 0 && configured != 1) || iterations < 0) {
        set_diagnostic(diagnostic, "screen lock settings file is unsupported");
        return std::nullopt;
    }
    ScreenLockSettings settings{};
    settings.enabled = enabled == 1;
    settings.configured = configured == 1;
    settings.iterations = static_cast<unsigned int>(iterations);
    if (settings.configured) {
        if (!decode_hex(values["salt"], &settings.salt) ||
            !decode_hex(values["verifier"], &settings.verifier)) {
            set_diagnostic(diagnostic, "screen lock settings verifier is invalid");
            return std::nullopt;
        }
    } else if (values["salt"] != "-" || values["verifier"] != "-") {
        set_diagnostic(diagnostic, "screen lock settings file is unsupported");
        return std::nullopt;
    }
    if (!screen_lock_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "screen lock settings are invalid");
        return std::nullopt;
    }
    return settings;
}

bool save_screen_lock_settings(const fs::path& path, const ScreenLockSettings& settings,
                               std::string* diagnostic) {
    if (path.empty() || path.parent_path().empty() || !screen_lock_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "screen lock settings are invalid");
        return false;
    }
    const std::string salt = settings.configured
        ? encode_hex(settings.salt.data(), settings.salt.size()) : "-";
    const std::string verifier = settings.configured
        ? encode_hex(settings.verifier.data(), settings.verifier.size()) : "-";
    const std::string content = "version=" + std::to_string(kFormatVersion) + "\n" +
                                "enabled=" + std::to_string(settings.enabled ? 1 : 0) + "\n" +
                                "configured=" + std::to_string(settings.configured ? 1 : 0) + "\n" +
                                "iterations=" + std::to_string(settings.iterations) + "\n" +
                                "salt=" + salt + "\n" + "verifier=" + verifier + "\n";
    const fs::path temporary = path.string() + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                          0600);
    if (fd < 0) {
        set_diagnostic(diagnostic,
                       "unable to create screen lock settings: " + std::string(std::strerror(errno)));
        return false;
    }
    const bool written = write_all(fd, content) && ::fsync(fd) == 0;
    const int close_status = ::close(fd);
    if (!written || close_status != 0) {
        ::unlink(temporary.c_str());
        set_diagnostic(diagnostic, "unable to write screen lock settings");
        return false;
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        set_diagnostic(diagnostic,
                       "unable to replace screen lock settings: " + std::string(std::strerror(errno)));
        return false;
    }
    const int parent_fd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd < 0) {
        set_diagnostic(diagnostic, "unable to sync screen lock settings directory: " +
                                       std::string(std::strerror(errno)));
        return false;
    }
    const int sync_status = ::fsync(parent_fd);
    const int parent_close_status = ::close(parent_fd);
    if (sync_status != 0 || parent_close_status != 0) {
        set_diagnostic(diagnostic, "unable to sync screen lock settings directory");
        return false;
    }
    return true;
}

}  // namespace micropanel_touch::platform
