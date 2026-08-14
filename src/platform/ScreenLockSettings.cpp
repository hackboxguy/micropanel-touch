#include "platform/ScreenLockSettings.h"
#include "platform/SettingsFile.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>

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

bool all_zero(const unsigned char* values, std::size_t count) {
    return std::all_of(values, values + count, [](unsigned char value) { return value == 0U; });
}

}  // namespace

bool ScreenLockAttemptLimiter::allows(std::chrono::steady_clock::time_point now) const {
    return remaining(now).count() == 0;
}

std::chrono::seconds ScreenLockAttemptLimiter::remaining(
    std::chrono::steady_clock::time_point now) const {
    if (now >= retry_after_) {
        return std::chrono::seconds{0};
    }
    const auto delay = retry_after_ - now;
    const auto truncated = std::chrono::duration_cast<std::chrono::seconds>(delay);
    return truncated + (truncated < delay ? std::chrono::seconds{1}
                                          : std::chrono::seconds{0});
}

std::chrono::seconds ScreenLockAttemptLimiter::record_failure(
    std::chrono::steady_clock::time_point now) {
    ++failed_attempts_;
    if (failed_attempts_ < kScreenLockFailuresBeforeDelay) {
        return std::chrono::seconds{0};
    }
    std::chrono::seconds delay = kScreenLockInitialRetryDelay;
    for (unsigned int index = kScreenLockFailuresBeforeDelay; index < failed_attempts_;
         ++index) {
        delay = std::min(delay * 2, kScreenLockMaximumRetryDelay);
    }
    retry_after_ = now + delay;
    return delay;
}

void ScreenLockAttemptLimiter::record_success() {
    failed_attempts_ = 0U;
    retry_after_ = {};
}

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
    SettingsFileError error = SettingsFileError::None;
    const auto values = load_settings_file(path, kMaximumFileBytes, kKeys.data(), kKeys.size(),
                                           0600, &error);
    if (!values.has_value()) {
        if (error == SettingsFileError::Missing) {
            return std::nullopt;
        }
        if (error == SettingsFileError::Metadata) {
            set_diagnostic(diagnostic, "screen lock settings file is invalid");
        } else if (error == SettingsFileError::Read) {
            set_diagnostic(diagnostic, "unable to read screen lock settings");
        } else if (error == SettingsFileError::InvalidLine) {
            set_diagnostic(diagnostic, "screen lock settings file contains an invalid line");
        } else if (error == SettingsFileError::UnknownOrRepeatedKey) {
            set_diagnostic(diagnostic,
                           "screen lock settings file contains an unknown or repeated key");
        } else if (error == SettingsFileError::Incomplete) {
            set_diagnostic(diagnostic, "screen lock settings file is incomplete");
        } else {
            set_diagnostic(diagnostic, "unable to open screen lock settings");
        }
        return std::nullopt;
    }
    int version = 0;
    int enabled = 0;
    int configured = 0;
    int iterations = 0;
    if (!parse_integer(values->at("version"), &version) ||
        !parse_integer(values->at("enabled"), &enabled) ||
        !parse_integer(values->at("configured"), &configured) ||
        !parse_integer(values->at("iterations"), &iterations) || version != kFormatVersion ||
        (enabled != 0 && enabled != 1) || (configured != 0 && configured != 1) || iterations < 0) {
        set_diagnostic(diagnostic, "screen lock settings file is unsupported");
        return std::nullopt;
    }
    ScreenLockSettings settings{};
    settings.enabled = enabled == 1;
    settings.configured = configured == 1;
    settings.iterations = static_cast<unsigned int>(iterations);
    if (settings.configured) {
        if (!decode_hex(values->at("salt"), &settings.salt) ||
            !decode_hex(values->at("verifier"), &settings.verifier)) {
            set_diagnostic(diagnostic, "screen lock settings verifier is invalid");
            return std::nullopt;
        }
    } else if (values->at("salt") != "-" || values->at("verifier") != "-") {
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
    SettingsFileError error = SettingsFileError::None;
    if (!save_settings_file(path, content, 0600, &error)) {
        if (error == SettingsFileError::Create) {
            set_diagnostic(diagnostic, "unable to create screen lock settings");
        } else if (error == SettingsFileError::Write) {
            set_diagnostic(diagnostic, "unable to write screen lock settings");
        } else if (error == SettingsFileError::Replace) {
            set_diagnostic(diagnostic, "unable to replace screen lock settings");
        } else {
            set_diagnostic(diagnostic, "unable to sync screen lock settings directory");
        }
        return false;
    }
    return true;
}

}  // namespace micropanel_touch::platform
