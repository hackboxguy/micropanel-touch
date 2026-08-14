#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/ScreenLockSettings.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using micropanel_touch::platform::ScreenLockSettings;

int main() {
    using namespace micropanel_touch::platform;

    assert(screen_lock_pin_is_valid("1234"));
    assert(screen_lock_pin_is_valid("1234567890"));
    assert(!screen_lock_pin_is_valid("123"));
    assert(!screen_lock_pin_is_valid("12345678901"));
    assert(!screen_lock_pin_is_valid("12a4"));
    assert(screen_lock_settings_are_valid(ScreenLockSettings{}));

    ScreenLockAttemptLimiter limiter;
    const auto epoch = std::chrono::steady_clock::time_point{};
    for (unsigned int attempt = 0U; attempt + 1U < kScreenLockFailuresBeforeDelay; ++attempt) {
        assert(limiter.allows(epoch));
        assert(limiter.record_failure(epoch).count() == 0);
    }
    assert(limiter.record_failure(epoch) == kScreenLockInitialRetryDelay);
    assert(!limiter.allows(epoch));
    assert(limiter.remaining(epoch) == kScreenLockInitialRetryDelay);
    assert(limiter.allows(epoch + kScreenLockInitialRetryDelay));
    assert(limiter.record_failure(epoch + kScreenLockInitialRetryDelay) ==
           kScreenLockInitialRetryDelay * 2);
    limiter.record_success();
    assert(limiter.allows(epoch));
    assert(limiter.record_failure(epoch).count() == 0);

    char directory_template[] = "/tmp/micropanel-touch-screen-lock-test-XXXXXX";
    const char* const directory = ::mkdtemp(directory_template);
    assert(directory != nullptr);
    const std::filesystem::path path = std::filesystem::path(directory) / "screen-lock.conf";
    std::string diagnostic;

    ScreenLockSettings settings;
    assert(set_screen_lock_pin(&settings, "123456", &diagnostic));
    assert(settings.configured);
    assert(!settings.enabled);
    assert(verify_screen_lock_pin(settings, "123456"));
    assert(!verify_screen_lock_pin(settings, "654321"));
    settings.enabled = true;
    assert(screen_lock_settings_are_valid(settings));
    assert(save_screen_lock_settings(path, settings, &diagnostic));

    struct stat metadata {};
    assert(::stat(path.c_str(), &metadata) == 0);
    assert((metadata.st_mode & 0777) == 0600);
    const auto loaded = load_screen_lock_settings(path, &diagnostic);
    assert(loaded.has_value());
    assert(loaded->enabled);
    assert(verify_screen_lock_pin(*loaded, "123456"));

    assert(::chmod(path.c_str(), 0644) == 0);
    assert(!load_screen_lock_settings(path, &diagnostic).has_value());
    assert(diagnostic == "screen lock settings file is invalid");
    assert(::chmod(path.c_str(), 0600) == 0);

    {
        std::ofstream corrupt(path);
        corrupt << "version=1\nenabled=1\nconfigured=0\niterations=210000\nsalt=-\nverifier=-\n";
    }
    assert(!load_screen_lock_settings(path, &diagnostic).has_value());
    assert(diagnostic == "screen lock settings are invalid");
    std::filesystem::remove_all(directory);
    return 0;
}
