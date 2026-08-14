#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace micropanel_touch::platform {

constexpr std::size_t kScreenLockPinMinimumDigits = 4U;
constexpr std::size_t kScreenLockPinMaximumDigits = 10U;
constexpr unsigned int kScreenLockPbkdf2Iterations = 210000U;
constexpr std::size_t kScreenLockSaltBytes = 16U;
constexpr std::size_t kScreenLockVerifierBytes = 32U;

// This type deliberately contains only a salted verifier.  A PIN must remain
// in an LVGL textarea or a short-lived string_view while it is being checked.
struct ScreenLockSettings {
    bool enabled{false};
    bool configured{false};
    unsigned int iterations{kScreenLockPbkdf2Iterations};
    std::array<unsigned char, kScreenLockSaltBytes> salt{};
    std::array<unsigned char, kScreenLockVerifierBytes> verifier{};
};

bool screen_lock_pin_is_valid(std::string_view pin);
bool screen_lock_settings_are_valid(const ScreenLockSettings& settings);

// Generates a new random salt and derives a PBKDF2-HMAC-SHA-256 verifier.
// The caller is responsible for durable persistence after this succeeds.
bool set_screen_lock_pin(ScreenLockSettings* settings, std::string_view pin,
                         std::string* diagnostic = nullptr);
bool verify_screen_lock_pin(const ScreenLockSettings& settings, std::string_view pin);

std::optional<ScreenLockSettings> load_screen_lock_settings(
    const std::filesystem::path& path, std::string* diagnostic = nullptr);
bool save_screen_lock_settings(const std::filesystem::path& path,
                               const ScreenLockSettings& settings,
                               std::string* diagnostic = nullptr);

}  // namespace micropanel_touch::platform
