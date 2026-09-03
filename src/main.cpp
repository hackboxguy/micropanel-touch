#include "core/ActionCompiler.h"
#include "core/LegacyConfig.h"
#include "platform/ActionService.h"
#include "platform/CommandService.h"
#include "platform/ControlServer.h"
#include "platform/DisplayBackend.h"
#include "platform/DisplayBrightnessSettings.h"
#include "platform/DisplaySleep.h"
#include "platform/DisplayStandbySettings.h"
#include "platform/IotAgentStatus.h"
#include "platform/IotAgentSettings.h"
#include "platform/ScreenLockSettings.h"
#include "platform/FrameCapture.h"
#include "platform/NetworkInfo.h"
#include "platform/NetworkApplyService.h"
#include "platform/PrivilegedBroker.h"
#include "platform/SystemUpdateService.h"
#include "platform/PanelProfile.h"
#include "platform/SyntheticKeypadInput.h"
#include "platform/SyntheticTouchInput.h"
#include "platform/AboutInfo.h"
#include "platform/HardwareInfo.h"
#include "platform/NetworkInterfaceDetail.h"
#include "platform/NetworkTestService.h"
#include "platform/StorageHealth.h"
#include "platform/SystemStats.h"
#include "platform/TouchCalibration.h"
#include "platform/TouchInput.h"
#include "platform/WifiScan.h"
#include "core/UiEventQueue.h"
#include "ui/LegacyUi.h"
#include "ui/StarterConfig.h"
#include "ui/StarterUi.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <lvgl.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>

namespace {

std::atomic_bool keep_running{true};
constexpr auto kDisplayDiscoveryRetry = std::chrono::milliseconds(250);
constexpr auto kDisplayDiscoveryTimeout = std::chrono::seconds(15);
constexpr unsigned int kMaximumTimerSleepMs = 20U;
constexpr unsigned int kSleepingTimerSleepMs = 100U;

struct Options {
    bool probe_only{false};
    bool no_input{false};
    bool portrait{false};
    std::string validate_config_path;
    std::string legacy_config_path;
    std::string control_socket_path;
    std::string privileged_broker_socket_path;
    std::string data_dir_path;
    std::string fallback_data_dir_path;
    std::string runtime_dir_path;
    std::string static_ip_interface{"eth0"};
    std::string framebuffer;
    std::string input;
    std::string config_path{"screens/config-basic.json"};
    std::string theme;
    unsigned int run_seconds{0};
};

void on_signal(int) {
    keep_running.store(false);
}

void print_usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n"
        << "  --probe                 Print DRM→fbdev, backlight, and touch discovery\n"
        << "  --fbdev PATH            Override automatic framebuffer selection\n"
        << "  --input PATH            Override automatic touch event selection\n"
        << "  --config PATH           Starter JSON config (default: screens/config-basic.json)\n"
        << "  --legacy-config PATH    Render menus/static lists from a legacy JSON config\n"
        << "  --control-socket PATH   Enable the owner-only development control socket\n"
        << "  --privileged-broker-socket PATH\n"
        << "                         Opt in to the root-owned static-IP broker\n"
        << "  --data-dir PATH        Persistent action-state directory\n"
        << "  --fallback-data-dir PATH\n"
        << "                         Volatile action-state fallback if --data-dir is unavailable\n"
        << "  --runtime-dir PATH     Volatile runtime directory\n"
        << "  --static-ip-interface NAME\n"
        << "                         Interface used with the static-IP broker (default: eth0)\n"
        << "  --validate-config PATH  Validate a legacy JSON config and print parity counts\n"
        << "  --theme NAME_OR_PATH    Override the configured skin\n"
        << "  --no-input              Run without a touch device\n"
        << "  --portrait              Rotate the UI to portrait (320x480)\n"
        << "  --run-seconds N         Exit after N seconds (hardware smoke testing)\n"
        << "  --help                  Show this help\n";
}

bool parse_unsigned(const std::string& value, unsigned int* result) {
    try {
        const unsigned long parsed = std::stoul(value);
        if (parsed > static_cast<unsigned long>(UINT_MAX)) {
            return false;
        }
        *result = static_cast<unsigned int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_options(int argc, char* argv[], Options* options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--probe") {
            options->probe_only = true;
        } else if (argument == "--no-input") {
            options->no_input = true;
        } else if (argument == "--portrait") {
            options->portrait = true;
        } else if (argument == "--fbdev" || argument == "--input" || argument == "--config" ||
                   argument == "--theme" || argument == "--validate-config" ||
                   argument == "--legacy-config" || argument == "--control-socket" ||
                   argument == "--privileged-broker-socket" ||
                   argument == "--data-dir" || argument == "--fallback-data-dir" ||
                   argument == "--runtime-dir" ||
                   argument == "--static-ip-interface" ||
                   argument == "--run-seconds") {
            if (++index >= argc) {
                std::cerr << argument << " requires a value\n";
                return false;
            }
            const std::string value = argv[index];
            if (argument == "--fbdev") {
                options->framebuffer = value;
            } else if (argument == "--input") {
                options->input = value;
            } else if (argument == "--config") {
                options->config_path = value;
            } else if (argument == "--theme") {
                options->theme = value;
            } else if (argument == "--validate-config") {
                options->validate_config_path = value;
            } else if (argument == "--legacy-config") {
                options->legacy_config_path = value;
            } else if (argument == "--control-socket") {
                options->control_socket_path = value;
            } else if (argument == "--privileged-broker-socket") {
                options->privileged_broker_socket_path = value;
            } else if (argument == "--data-dir") {
                options->data_dir_path = value;
            } else if (argument == "--fallback-data-dir") {
                options->fallback_data_dir_path = value;
            } else if (argument == "--runtime-dir") {
                options->runtime_dir_path = value;
            } else if (argument == "--static-ip-interface") {
                options->static_ip_interface = value;
            } else if (!parse_unsigned(value, &options->run_seconds)) {
                std::cerr << "Invalid --run-seconds value: " << value << '\n';
                return false;
            }
        } else if (argument == "--help") {
            print_usage(argv[0]);
            std::exit(EXIT_SUCCESS);
        } else {
            std::cerr << "Unknown option: " << argument << '\n';
            return false;
        }
    }
    const auto is_absolute_or_empty = [](const std::string& path) {
        return path.empty() || std::filesystem::path(path).is_absolute();
    };
    return is_absolute_or_empty(options->data_dir_path) &&
           is_absolute_or_empty(options->fallback_data_dir_path) &&
           is_absolute_or_empty(options->runtime_dir_path) &&
           (options->fallback_data_dir_path.empty() || !options->data_dir_path.empty());
}

void print_touch_devices() {
    const auto devices = micropanel_touch::platform::TouchInput::enumerate();
    std::cout << "Touch candidates: " << devices.size() << '\n';
    for (const auto& device : devices) {
        std::cout << "  " << device.path << " (" << device.name << ")"
                  << " technology="
                  << micropanel_touch::platform::touch_technology_name(device.technology)
                  << " x=" << device.x_axis.minimum << ".." << device.x_axis.maximum
                  << " y=" << device.y_axis.minimum << ".." << device.y_axis.maximum
                  << " pressure=" << device.pressure_axis.minimum << ".."
                  << device.pressure_axis.maximum << '\n';
    }
}

std::optional<micropanel_touch::platform::DisplayTarget> discover_display(
    std::string* diagnostic) {
    const auto deadline = std::chrono::steady_clock::now() + kDisplayDiscoveryTimeout;
    do {
        const auto target = micropanel_touch::platform::DisplayBackend::discover(diagnostic);
        if (target.has_value()) {
            return target;
        }
        std::this_thread::sleep_for(kDisplayDiscoveryRetry);
    } while (std::chrono::steady_clock::now() < deadline);
    return std::nullopt;
}

std::filesystem::path resolve_config_path(const std::string& requested, const char* executable) {
    namespace fs = std::filesystem;
    const fs::path requested_path(requested);
    std::error_code ec;
    if (fs::exists(requested_path, ec)) {
        return requested_path;
    }

    const fs::path executable_directory = fs::absolute(executable, ec).parent_path();
    const std::string filename = requested_path.filename().string();
    const std::array<fs::path, 2> candidates{
        executable_directory.parent_path() / "screens" / filename,
        executable_directory.parent_path().parent_path() / "share" / "micropanel-touch" / "screens" /
            filename,
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
    }
    return requested_path;
}

std::optional<micropanel_touch::core::ExecutionContext> make_execution_context(
    const std::filesystem::path& config_path, const char* executable,
    const std::string& configured_data_dir, const std::string& configured_fallback_data_dir,
    const std::string& configured_runtime_dir, std::string* diagnostic) {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path executable_path = fs::weakly_canonical(fs::absolute(executable, error), error);
    if (error || executable_path.parent_path().empty()) {
        *diagnostic = "Unable to resolve executable location for ExecutionContext";
        return std::nullopt;
    }
    const fs::path executable_directory = executable_path.parent_path();
    // A source-tree binary lives at <repo>/build/micropanel-touch, while an
    // installed one lives at <prefix>/usr/bin/micropanel-touch. Keep the
    // package root explicit so config and handler paths stay inside it.
    const bool installed_layout = executable_directory.filename() == "bin" &&
                                  executable_directory.parent_path().filename() == "usr";
    const fs::path home = (installed_layout ? executable_directory.parent_path().parent_path()
                                             : executable_directory.parent_path())
                              .lexically_normal();
    const fs::path absolute_config_path = fs::weakly_canonical(fs::absolute(config_path, error), error);
    if (error) {
        *diagnostic = "Unable to resolve config location for ExecutionContext";
        return std::nullopt;
    }
    const fs::path source_handler_directory = home / "handlers";
    std::error_code handler_layout_error;
    const bool source_layout = fs::is_directory(source_handler_directory, handler_layout_error);
    // A missing source-tree handlers directory is normal in the installed
    // layout: production handlers live under <prefix>/usr/bin instead.
    if (handler_layout_error &&
        handler_layout_error != std::errc::no_such_file_or_directory) {
        *diagnostic = "Unable to inspect development handler directory: " + handler_layout_error.message();
        return std::nullopt;
    }
    const fs::path handler_directory = source_layout ? source_handler_directory : home / "usr" / "bin";
    const auto make_context = [&home, &absolute_config_path, &handler_directory](const fs::path& data,
                                                                                   const fs::path& runtime) {
        return micropanel_touch::core::ExecutionContext{
            home,
            absolute_config_path.parent_path().lexically_normal(),
            data,
            data / "logs",
            runtime,
            handler_directory,
        };
    };
    const fs::path data = configured_data_dir.empty() ? home / ".runtime-data"
                                                        : fs::path(configured_data_dir);
    const fs::path runtime = configured_runtime_dir.empty() ? data / "run"
                                                             : fs::path(configured_runtime_dir);
    auto context = make_context(data, runtime);
    if (!context.validate(diagnostic)) {
        return std::nullopt;
    }
    const auto prepare_log_directory = [&error](const auto& selected, std::string* message) {
        error.clear();
        std::filesystem::create_directories(selected.log_dir, error);
        if (error) {
            *message = "Unable to create action log directory: " + error.message();
            return false;
        }
        std::filesystem::permissions(selected.log_dir, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, error);
        if (error) {
            *message = "Unable to protect action log directory: " + error.message();
            return false;
        }
        return true;
    };
    std::string primary_error;
    const auto storage_health = configured_data_dir.empty()
                                    ? micropanel_touch::platform::StorageHealth{
                                          micropanel_touch::platform::StoragePersistence::persistent, {}}
                                    : micropanel_touch::platform::inspect_storage(context.data_dir);
    if (storage_health.persistence == micropanel_touch::platform::StoragePersistence::persistent &&
        prepare_log_directory(context, &primary_error)) {
        return context;
    }
    if (storage_health.persistence != micropanel_touch::platform::StoragePersistence::persistent) {
        primary_error = storage_health.diagnostic;
    }
    if (configured_fallback_data_dir.empty()) {
        *diagnostic = primary_error;
        return std::nullopt;
    }
    auto fallback = make_context(fs::path(configured_fallback_data_dir), runtime);
    if (!fallback.validate(diagnostic)) {
        return std::nullopt;
    }
    std::string fallback_error;
    if (!prepare_log_directory(fallback, &fallback_error)) {
        *diagnostic = primary_error + "; fallback unavailable: " + fallback_error;
        return std::nullopt;
    }
    *diagnostic = "Persistent action storage unavailable (" + primary_error +
                  "); using volatile fallback " + fallback.data_dir.string();
    return fallback;
}

std::string system_update_status() {
    std::string slot = "unknown";
    std::ifstream cmdline("/proc/cmdline");
    std::string command_line;
    std::getline(cmdline, command_line);
    if (command_line.find("root=LABEL=MP_ROOT_A") != std::string::npos) {
        slot = "A";
    } else if (command_line.find("root=LABEL=MP_ROOT_B") != std::string::npos) {
        slot = "B";
    }

    std::string version = "unknown";
    std::ifstream version_file("/etc/incremental-version.txt");
    std::getline(version_file, version);
    if (version.empty()) {
        version = "unknown";
    }

    // The root-owned durable update state is intentionally unreadable by the
    // HMI.  The commit service publishes this bounded, world-readable summary
    // after every candidate/normal boot instead.
    std::string state = "no candidate update recorded";
    std::ifstream state_file("/run/micropanel-touch-update/status");
    std::string line;
    while (std::getline(state_file, line)) {
        if (line == "state=committed") {
            state = "committed";
            break;
        }
        if (line == "state=candidate-armed") {
            state = "candidate boot pending";
            break;
        }
        if (line == "state=fallback") {
            state = "candidate abandoned; committed slot retained";
            break;
        }
    }
    return "Running slot: " + slot + "\nVersion: " + version + "\nUpdate state: " + state;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!options.validate_config_path.empty()) {
        std::string diagnostic;
        const auto config =
            micropanel_touch::core::LegacyConfig::load(options.validate_config_path, &diagnostic);
        if (!config.has_value()) {
            std::cerr << "Invalid config " << options.validate_config_path << ": " << diagnostic << '\n';
            return EXIT_FAILURE;
        }
        const auto counts = config->counts();
        std::cout << "Valid config: " << options.validate_config_path << '\n'
                  << "module_declarations=" << counts.module_declarations << '\n'
                  << "submenu_references=" << counts.submenu_references << '\n';
        return EXIT_SUCCESS;
    }

    if (options.probe_only) {
        std::cout << micropanel_touch::platform::DisplayBackend::format_probe();
        print_touch_devices();
        return EXIT_SUCCESS;
    }

    std::string config_diagnostic;
    const bool use_legacy_config = !options.legacy_config_path.empty();
    if (use_legacy_config && !options.privileged_broker_socket_path.empty()) {
        std::cerr << "--privileged-broker-socket requires the starter UI\n";
        return EXIT_FAILURE;
    }
    if (!options.privileged_broker_socket_path.empty() &&
        !std::filesystem::path(options.privileged_broker_socket_path).is_absolute()) {
        std::cerr << "--privileged-broker-socket must be an absolute path\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path config_path = resolve_config_path(
        use_legacy_config ? options.legacy_config_path : options.config_path, argv[0]);
    std::optional<micropanel_touch::ui::StarterConfig> starter_config;
    std::optional<micropanel_touch::core::LegacyConfig> legacy_config;
    std::string requested_theme;
    if (use_legacy_config) {
        legacy_config = micropanel_touch::core::LegacyConfig::load(config_path, &config_diagnostic);
        if (!legacy_config.has_value()) {
            std::cerr << "Unable to load legacy config " << config_path << ": "
                      << config_diagnostic << '\n';
            return EXIT_FAILURE;
        }
        requested_theme = options.theme.empty() ? "dark" : options.theme;
    } else {
        starter_config = micropanel_touch::ui::StarterConfig::load(config_path, &config_diagnostic);
        if (!starter_config.has_value()) {
            std::cerr << "Unable to load starter config " << config_path << ": "
                      << config_diagnostic << '\n';
            return EXIT_FAILURE;
        }
        requested_theme = options.theme.empty() ? starter_config->theme() : options.theme;
    }

    std::string framebuffer = options.framebuffer;
    if (framebuffer.empty()) {
        std::string diagnostic;
        const auto target = discover_display(&diagnostic);
        if (!target.has_value()) {
            std::cerr << "Display discovery failed: " << diagnostic << "\nRun with --probe for details.\n";
            return EXIT_FAILURE;
        }
        framebuffer = target->framebuffer.string();
        std::cout << "Selected " << target->drm_by_path << " / " << target->connector
                  << " -> " << framebuffer << '\n';
    }

    lv_init();
    lv_display_t* const display = lv_linux_fbdev_create();
    if (display == nullptr || lv_linux_fbdev_set_file(display, framebuffer.c_str()) != LV_RESULT_OK) {
        std::cerr << "Unable to initialize framebuffer " << framebuffer << '\n';
        return EXIT_FAILURE;
    }

    if (options.portrait) {
        // This is a development preview only. The shipping portrait profile
        // rotates in the panel controller; LVGL runtime rotation also rotates
        // pointer coordinates and is deliberately not used for normal boot.
        lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
        std::cout << "UI orientation: portrait ("
                  << lv_display_get_horizontal_resolution(display) << 'x'
                  << lv_display_get_vertical_resolution(display) << ")\n";
    }

    const std::filesystem::path starter_config_path =
        resolve_config_path("screens/config-basic.json", argv[0]);
    micropanel_touch::ui::UiTheme theme((use_legacy_config ? starter_config_path : config_path)
                                        .parent_path() / "themes");
    std::string theme_diagnostic;
    if (!theme.activate(requested_theme, display, &theme_diagnostic)) {
        std::cerr << "Unable to load skin " << requested_theme << ": " << theme_diagnostic
                  << "; falling back to dark\n";
        if (requested_theme == "dark" || !theme.activate("dark", display, &theme_diagnostic)) {
            std::cerr << "Unable to load fallback dark skin: " << theme_diagnostic << '\n';
            return EXIT_FAILURE;
        }
    }
    std::cout << "Using skin " << theme.active_skin().name << '\n';

    micropanel_touch::core::UiEventQueue event_queue;
    const int native_width = lv_display_get_original_horizontal_resolution(display);
    const int native_height = lv_display_get_original_vertical_resolution(display);
    const auto logical_to_native = [display, native_width, native_height](
                                        micropanel_touch::platform::TouchPoint point) {
        switch (lv_display_get_rotation(display)) {
            case LV_DISPLAY_ROTATION_90:
                return micropanel_touch::platform::TouchPoint{
                    point.y, native_height - point.x - 1};
            case LV_DISPLAY_ROTATION_180:
                return micropanel_touch::platform::TouchPoint{
                    native_width - point.x - 1, native_height - point.y - 1};
            case LV_DISPLAY_ROTATION_270:
                return micropanel_touch::platform::TouchPoint{
                    native_width - point.y - 1, point.x};
            case LV_DISPLAY_ROTATION_0:
            default:
                return point;
        }
    };
    const auto native_to_logical = [display, native_width, native_height](
                                        micropanel_touch::platform::TouchPoint point) {
        switch (lv_display_get_rotation(display)) {
            case LV_DISPLAY_ROTATION_90:
                return micropanel_touch::platform::TouchPoint{
                    native_height - point.y - 1, point.x};
            case LV_DISPLAY_ROTATION_180:
                return micropanel_touch::platform::TouchPoint{
                    native_width - point.x - 1, native_height - point.y - 1};
            case LV_DISPLAY_ROTATION_270:
                return micropanel_touch::platform::TouchPoint{
                    point.y, native_width - point.x - 1};
            case LV_DISPLAY_ROTATION_0:
            default:
                return point;
        }
    };
    const std::filesystem::path touch_calibration_path = !options.data_dir_path.empty()
        ? std::filesystem::path(options.data_dir_path) / "touch-calibration.conf"
        : (!options.fallback_data_dir_path.empty()
               ? std::filesystem::path(options.fallback_data_dir_path) / "touch-calibration.conf"
               : std::filesystem::path{});
    const std::filesystem::path display_settings_path = !options.data_dir_path.empty()
        ? std::filesystem::path(options.data_dir_path) / "display-settings.conf"
        : (!options.fallback_data_dir_path.empty()
               ? std::filesystem::path(options.fallback_data_dir_path) / "display-settings.conf"
               : std::filesystem::path{});
    const std::filesystem::path display_brightness_settings_path = !options.data_dir_path.empty()
        ? std::filesystem::path(options.data_dir_path) / "display-brightness.conf"
        : (!options.fallback_data_dir_path.empty()
               ? std::filesystem::path(options.fallback_data_dir_path) / "display-brightness.conf"
               : std::filesystem::path{});
    // A lock verifier is meaningful only when it survives the appliance's
    // volatile root. Unlike display preferences, never fall back to /run.
    const std::filesystem::path screen_lock_settings_path = !options.data_dir_path.empty()
        ? std::filesystem::path(options.data_dir_path) / "screen-lock.conf"
        : std::filesystem::path{};
    micropanel_touch::platform::DisplayStandbySettings display_standby_settings{
        starter_config.has_value() && starter_config->display_sleep_seconds() != 0U,
        starter_config.has_value() && starter_config->display_sleep_seconds() != 0U
            ? starter_config->display_sleep_seconds()
            : 60U};
    if (!use_legacy_config && !display_settings_path.empty()) {
        std::string settings_diagnostic;
        if (const auto saved = micropanel_touch::platform::load_display_standby_settings(
                display_settings_path, &settings_diagnostic);
            saved.has_value()) {
            display_standby_settings = *saved;
            std::cout << "Loaded persistent display standby settings\n";
        } else if (!settings_diagnostic.empty()) {
            std::cerr << "Ignoring display standby settings: " << settings_diagnostic << '\n';
        }
    }
    micropanel_touch::platform::DisplayBrightnessSettings display_brightness_settings;
    if (!use_legacy_config && !display_brightness_settings_path.empty()) {
        std::string settings_diagnostic;
        if (const auto saved = micropanel_touch::platform::load_display_brightness_settings(
                display_brightness_settings_path, &settings_diagnostic);
            saved.has_value()) {
            display_brightness_settings = *saved;
            std::cout << "Loaded persistent display brightness settings\n";
        } else if (!settings_diagnostic.empty()) {
            std::cerr << "Ignoring display brightness settings: " << settings_diagnostic << '\n';
        }
    }
    micropanel_touch::platform::ScreenLockSettings screen_lock_settings;
    bool screen_lock_session_locked = false;
    if (!use_legacy_config && !screen_lock_settings_path.empty()) {
        std::string settings_diagnostic;
        if (const auto saved = micropanel_touch::platform::load_screen_lock_settings(
                screen_lock_settings_path, &settings_diagnostic);
            saved.has_value()) {
            screen_lock_settings = *saved;
            // A configured, enabled screen lock also protects a restarted HMI
            // session. Fresh images retain the default disabled state.
            screen_lock_session_locked = screen_lock_settings.enabled;
            std::cout << "Loaded persistent screen lock settings\n";
        } else if (!settings_diagnostic.empty()) {
            std::cerr << "Ignoring screen lock settings: " << settings_diagnostic << '\n';
        }
    }
    std::uint64_t next_touch_sample_sequence = 1U;
    std::unique_ptr<micropanel_touch::platform::TouchInput> touch;
    std::optional<micropanel_touch::platform::PanelProfile> selected_panel_profile;
    if (!options.no_input) {
        std::string diagnostic;
        touch = options.input.empty()
            ? micropanel_touch::platform::TouchInput::open_auto(&diagnostic)
            : micropanel_touch::platform::TouchInput::open(options.input, &diagnostic);
        if (touch == nullptr) {
            std::cerr << "Touch initialization failed: " << diagnostic << '\n';
            return EXIT_FAILURE;
        }
        // LVGL rotates pointer coordinates after this callback. Keep the raw
        // touch mapper in the panel's native coordinate space to avoid applying
        // portrait rotation twice.
        touch->set_display_size(native_width, native_height);
        if (const auto profile = micropanel_touch::platform::select_panel_profile(
                touch->device().technology, native_width, native_height);
            profile.has_value()) {
            selected_panel_profile = *profile;
            std::cout << "Selected panel profile " << profile->id;
            std::cout << " (image boot configured by " << profile->boot_configurator << ')';
            std::cout << '\n';
        } else {
            std::cerr << "No named panel profile matches "
                      << micropanel_touch::platform::touch_technology_name(
                             touch->device().technology)
                      << ' ' << native_width << 'x' << native_height
                      << "; continuing with generic input/display discovery\n";
        }
        if (!touch_calibration_path.empty()) {
            std::string calibration_diagnostic;
            const auto calibration =
                micropanel_touch::platform::load_touch_calibration(touch_calibration_path,
                                                                     &calibration_diagnostic);
            if (calibration.has_value()) {
                if (micropanel_touch::platform::touch_calibration_is_compatible(
                        *calibration, touch->device().x_axis, touch->device().y_axis,
                        native_width, native_height, &calibration_diagnostic)) {
                    touch->set_calibration(*calibration);
                    std::cout << "Loaded persistent touch calibration\n";
                } else {
                    std::cerr << "Ignoring touch calibration: " << calibration_diagnostic << '\n';
                }
            } else if (!calibration_diagnostic.empty()) {
                std::cerr << "Ignoring touch calibration: " << calibration_diagnostic << '\n';
            }
        }
        // Legacy configurations do not consume calibration sample events.
        // Avoid retaining one event for every physical press in that mode.
        if (!use_legacy_config) {
            touch->set_raw_touch_callback(
                [&event_queue, &next_touch_sample_sequence, native_to_logical](
                    const micropanel_touch::platform::TouchInput::RawTouchSample& sample) {
                    const auto screen_point = native_to_logical(sample.mapped);
                    event_queue.push({next_touch_sample_sequence++,
                                      micropanel_touch::core::TouchCalibrationRawSample{
                                          sample.raw.x, sample.raw.y,
                                          screen_point.x, screen_point.y}});
                });
        }
        touch->attach_to_lvgl();
        std::cout << "Using "
                  << micropanel_touch::platform::touch_technology_name(touch->device().technology)
                  << " touch device " << touch->device().path << " (" << touch->device().name << ")\n";
    }

    const std::filesystem::path dhcp_server_state_directory = options.data_dir_path.empty()
        ? std::filesystem::path{}
        : std::filesystem::path(options.data_dir_path).parent_path() /
              "micropanel-touch-network/dhcp-server";
    micropanel_touch::platform::NetworkInfoProvider network_provider(
        event_queue, options.static_ip_interface, dhcp_server_state_directory);
    micropanel_touch::platform::WifiScanProvider wifi_scan_provider(event_queue);
    micropanel_touch::platform::CommandService action_command_service(event_queue);
    micropanel_touch::platform::ActionService action_service(action_command_service, event_queue);
    std::shared_ptr<micropanel_touch::platform::SysfsBacklight> display_backlight;
    bool display_brightness_available = false;
    std::optional<micropanel_touch::platform::DisplaySleepController> display_sleep;
    if (!use_legacy_config && touch != nullptr && selected_panel_profile.has_value() &&
        selected_panel_profile->backlight_path.has_value()) {
        display_backlight = std::make_shared<micropanel_touch::platform::SysfsBacklight>(
            std::filesystem::path(*selected_panel_profile->backlight_path));
        std::string brightness_diagnostic;
        display_brightness_available =
            display_backlight->has_variable_brightness(&brightness_diagnostic);
        if (display_brightness_available) {
            if (!display_backlight->set_brightness_percent(display_brightness_settings.percent,
                                                           &brightness_diagnostic)) {
                std::cerr << "Unable to restore display brightness: " << brightness_diagnostic << '\n';
                display_brightness_available = false;
            }
        } else if (!brightness_diagnostic.empty()) {
            std::cout << "Display brightness unavailable: " << brightness_diagnostic << '\n';
        }
        display_sleep.emplace(
            std::chrono::seconds(display_standby_settings.enabled
                                     ? display_standby_settings.seconds : 0U),
            [display_backlight](bool enabled, std::string* diagnostic) {
                return display_backlight->set_enabled(enabled, diagnostic);
            },
            [display](bool enabled) {
                lv_timer_t* const refresh_timer = lv_display_get_refr_timer(display);
                if (enabled) {
                    lv_display_enable_invalidation(display, true);
                    if (refresh_timer != nullptr) {
                        lv_timer_resume(refresh_timer);
                    }
                    lv_obj_invalidate(lv_screen_active());
                } else {
                    lv_display_enable_invalidation(display, false);
                    if (refresh_timer != nullptr) {
                        lv_timer_pause(refresh_timer);
                    }
                }
            });
        touch->set_activity_callback([&display_sleep, display] {
            lv_display_trigger_activity(display);
            std::string diagnostic;
            const bool consume_wake_contact = display_sleep->on_input_activity(&diagnostic);
            if (!diagnostic.empty()) {
                std::cerr << "Display wake failed: " << diagnostic << '\n';
            }
            return consume_wake_contact;
        });
        if (display_standby_settings.enabled) {
            std::cout << "Display sleep enabled after " << display_standby_settings.seconds
                      << " seconds of inactivity\n";
        } else {
            std::cout << "Display sleep disabled by persistent setting\n";
        }
    } else if (!use_legacy_config) {
        std::cout << "Display sleep disabled: selected panel has no verified backlight path\n";
    }
    const auto frame_capture = [framebuffer](std::string* diagnostic) {
        return micropanel_touch::platform::capture_framebuffer_rgb565(framebuffer, diagnostic);
    };
    micropanel_touch::platform::ControlServer control_server(event_queue);
    std::unique_ptr<micropanel_touch::platform::NetworkApplyService> network_apply_service;
    std::unique_ptr<micropanel_touch::platform::SystemUpdateService> system_update_service;
    std::unique_ptr<micropanel_touch::ui::LegacyUi> legacy_ui;
    std::unique_ptr<micropanel_touch::ui::StarterUi> starter_ui;
    std::unique_ptr<micropanel_touch::platform::SyntheticKeypadInput> synthetic_keypad;
    std::unique_ptr<micropanel_touch::platform::SyntheticTouchInput> synthetic_touch;
    std::optional<micropanel_touch::core::ExecutionContext> execution_context;
    const auto apply_touch_calibration =
        [&touch, touch_calibration_path, native_width, native_height](
            const std::vector<micropanel_touch::platform::TouchCalibrationSample>& samples,
            std::string* diagnostic) {
            if (touch == nullptr || touch_calibration_path.empty()) {
                if (diagnostic != nullptr) {
                    *diagnostic = "persistent touch storage is unavailable";
                }
                return false;
            }
            const auto calibration = micropanel_touch::platform::solve_touch_calibration(
                samples, touch->device().x_axis, touch->device().y_axis,
                native_width, native_height, diagnostic);
            if (!calibration.has_value()) {
                return false;
            }
            if (!micropanel_touch::platform::save_touch_calibration(
                    touch_calibration_path, *calibration, diagnostic)) {
                return false;
            }
            touch->set_calibration(*calibration);
            return true;
        };
    const auto reset_touch_calibration =
        [&touch, touch_calibration_path](std::string* diagnostic) {
            if (touch == nullptr || touch_calibration_path.empty()) {
                if (diagnostic != nullptr) {
                    *diagnostic = "persistent touch storage is unavailable";
                }
                return false;
            }
            if (!micropanel_touch::platform::remove_touch_calibration(
                    touch_calibration_path, diagnostic)) {
                return false;
            }
            touch->clear_calibration();
            return true;
        };
    micropanel_touch::ui::StarterUi::TouchCalibrationApplyCallback
        touch_calibration_callback;
    micropanel_touch::ui::StarterUi::TouchCalibrationResetCallback
        touch_calibration_reset_callback;
    if (touch != nullptr && !touch_calibration_path.empty()) {
        touch_calibration_callback = apply_touch_calibration;
        touch_calibration_reset_callback = reset_touch_calibration;
    }
    if (!options.control_socket_path.empty()) {
        synthetic_touch = std::make_unique<micropanel_touch::platform::SyntheticTouchInput>();
        std::string synthetic_touch_diagnostic;
        if (!synthetic_touch->attach(&synthetic_touch_diagnostic)) {
            std::cerr << "Unable to initialize synthetic touch: " << synthetic_touch_diagnostic << '\n';
            return EXIT_FAILURE;
        }
        synthetic_keypad = std::make_unique<micropanel_touch::platform::SyntheticKeypadInput>();
        std::string synthetic_keypad_diagnostic;
        if (!synthetic_keypad->attach(&synthetic_keypad_diagnostic)) {
            std::cerr << "Unable to initialize synthetic keypad: " << synthetic_keypad_diagnostic << '\n';
            return EXIT_FAILURE;
        }
    }
    if (use_legacy_config) {
        legacy_ui = std::make_unique<micropanel_touch::ui::LegacyUi>(
            *legacy_config, event_queue, synthetic_touch.get(), frame_capture);
        legacy_ui->start();
    } else {
        std::string execution_context_diagnostic;
        execution_context =
            make_execution_context(config_path, argv[0], options.data_dir_path,
                                   options.fallback_data_dir_path, options.runtime_dir_path,
                                   &execution_context_diagnostic);
        if (execution_context.has_value()) {
            // Handlers stage their scratch files where the HMI keeps its own
            // runtime state - mode 700 and owned by this user - rather than
            // guessing a path or falling back to a shared /tmp. Exported once
            // here so the directory is named in one place.
            ::setenv("MICROPANEL_TOUCH_RUNTIME_DIR",
                     execution_context->runtime_dir.c_str(), 1);
        }
        if (!execution_context.has_value()) {
            std::cerr << "Action demo is unavailable: " << execution_context_diagnostic << '\n';
        } else if (!execution_context_diagnostic.empty()) {
            std::cerr << execution_context_diagnostic << '\n';
        }
        network_provider.start();
        if (!options.privileged_broker_socket_path.empty()) {
            network_apply_service =
                std::make_unique<micropanel_touch::platform::NetworkApplyService>(
                    event_queue, options.privileged_broker_socket_path);
            system_update_service =
                std::make_unique<micropanel_touch::platform::SystemUpdateService>(
                    event_queue, options.privileged_broker_socket_path);
            std::cout << "Network settings broker client enabled for " << options.static_ip_interface << '\n';
        }
        // Reading /proc and /sys is a handful of small file reads, so the UI
        // thread does it directly rather than through a worker: a thread here
        // would add a queue and a race to save nothing measurable. The reader
        // is stateful only because a CPU percentage needs two samples.
        auto system_stats_reader =
            std::make_shared<micropanel_touch::platform::SystemStatsReader>();
        micropanel_touch::ui::StarterUi::SystemServices system_services;
        system_services.system_stats = [system_stats_reader] {
            return system_stats_reader->read();
        };
        system_services.about_info = [] { return micropanel_touch::platform::read_about_info(); };
        system_services.hardware_info = [] {
            return micropanel_touch::platform::read_hardware_info();
        };
        // Same shape as the stats reader, and stateful for the same reason: a
        // byte rate needs two samples.
        auto interface_reader =
            std::make_shared<micropanel_touch::platform::NetworkInterfaceDetailReader>();
        system_services.network_interfaces = [interface_reader] {
            return interface_reader->interface_names();
        };
        system_services.network_interface = [interface_reader](const std::string& name) {
            return interface_reader->read(name);
        };
        // The diagnostics handler sits beside the others under the install
        // prefix; the execution context already knows where that is.
        // The handler sits beside the others under the install prefix, which
        // the execution context already resolved - source tree during
        // development, usr/bin once installed.
        const std::filesystem::path net_test_handler =
            execution_context.has_value()
                ? execution_context->handler_dir / "micropanel-touch-net-test"
                : std::filesystem::path{};
        auto network_tests = std::make_shared<micropanel_touch::platform::NetworkTestService>(
            event_queue, net_test_handler);
        system_services.start_network_test =
            [network_tests](std::uint64_t request_id,
                            micropanel_touch::platform::NetworkTestService::Test test,
                            const std::string& interface_name,
                            std::vector<std::string> arguments, std::string* diagnostic) {
                return network_tests->start(request_id, test, interface_name,
                                            std::move(arguments), diagnostic);
            };
        system_services.cancel_network_test = [network_tests] { network_tests->cancel(); };
        // A second runner, so the server occupies its own slot: it stays up
        // while the operator runs other tests, and leaving its screen does not
        // stop it.
        auto iperf_server = std::make_shared<micropanel_touch::platform::NetworkTestService>(
            event_queue, net_test_handler);
        system_services.start_iperf_server =
            [iperf_server](std::uint64_t request_id, const std::string& interface_name,
                           const std::string& port, std::string* diagnostic) {
                return iperf_server->start(
                    request_id, micropanel_touch::platform::NetworkTestService::Test::iperf_server,
                    interface_name, {port}, diagnostic);
            };
        system_services.stop_iperf_server = [iperf_server] { iperf_server->cancel(); };
        system_services.iperf_server_running = [iperf_server] { return iperf_server->is_running(); };
        if (net_test_handler.empty()) {
            // No context means no handler path, and a start that could only
            // fail is worse than a screen that says the capability is absent.
            system_services.start_network_test = nullptr;
        }
        // Power is a typed broker operation like every other privileged one:
        // the UI sends an enum, and the root side owns the command. Without a
        // broker socket the capability is absent rather than degraded, and the
        // screen says so.
        if (!options.privileged_broker_socket_path.empty()) {
            system_services.request_power =
                [socket = options.privileged_broker_socket_path](
                    micropanel_touch::core::PowerAction action, std::string* diagnostic) {
                    const auto reply = micropanel_touch::platform::PrivilegedBrokerClient::power(
                        socket, micropanel_touch::core::PowerOperation{action}, diagnostic);
                    if (!reply.ok && diagnostic != nullptr && !reply.message.empty()) {
                        *diagnostic = reply.message;
                    }
                    return reply.ok;
                };
        }

        // The IoT agent (xmproxysrv) answers its own JSON-RPC on a loopback
        // TCP port; the indicator on the IOT-Agent screen polls that. The
        // account form goes through the broker like every privileged change,
        // and what the panel remembers of it (never the password) lives with
        // the other preferences.
        constexpr std::uint16_t kIotAgentRpcPort = 40005U;
        auto iot_agent_monitor =
            std::make_shared<micropanel_touch::platform::IotAgentStatusMonitor>("127.0.0.1",
                                                                                kIotAgentRpcPort);
        system_services.iot_agent_status = [iot_agent_monitor] {
            return iot_agent_monitor->snapshot();
        };
        const std::filesystem::path iot_agent_settings_path = !options.data_dir_path.empty()
            ? std::filesystem::path(options.data_dir_path) / "iot-agent.conf"
            : (!options.fallback_data_dir_path.empty()
                   ? std::filesystem::path(options.fallback_data_dir_path) / "iot-agent.conf"
                   : std::filesystem::path{});
        system_services.iot_agent_settings = [iot_agent_settings_path] {
            std::string diagnostic;
            auto settings = micropanel_touch::platform::load_iot_agent_settings(
                iot_agent_settings_path, &diagnostic);
            if (!settings.has_value() && !diagnostic.empty()) {
                std::cerr << "IoT agent settings ignored: " << diagnostic << '\n';
            }
            return settings;
        };
        if (!options.privileged_broker_socket_path.empty()) {
            // Remembering the account is a convenience, not part of applying
            // it: the agent already has the file. A failure is logged, and the
            // owner types the account again next time.
            const auto remember = [iot_agent_settings_path](
                                      const micropanel_touch::platform::IotAgentSettings& settings) {
                std::string diagnostic;
                if (iot_agent_settings_path.empty() ||
                    !micropanel_touch::platform::save_iot_agent_settings(iot_agent_settings_path,
                                                                        settings, &diagnostic)) {
                    std::cerr << "IoT agent account applied but not remembered: "
                              << (diagnostic.empty() ? "no settings storage" : diagnostic) << '\n';
                }
            };
            system_services.apply_iot_agent_config =
                [socket = options.privileged_broker_socket_path, remember, iot_agent_monitor](
                    const micropanel_touch::core::IotAgentConfigOperation& operation,
                    std::string* diagnostic) {
                    const auto reply =
                        micropanel_touch::platform::PrivilegedBrokerClient::iot_agent_config(
                            socket, operation, diagnostic);
                    if (!reply.ok) {
                        if (diagnostic != nullptr && !reply.message.empty()) {
                            *diagnostic = reply.message;
                        }
                        return false;
                    }
                    iot_agent_monitor->reset();
                    micropanel_touch::platform::IotAgentSettings settings;
                    settings.user = operation.user;
                    settings.server = operation.server;
                    settings.port = operation.port;
                    settings.bosh = operation.bosh;
                    settings.bosh_url = operation.bosh_url;
                    settings.bosh_host = operation.bosh_host;
                    settings.admin = operation.admin;
                    settings.enabled = true;
                    remember(settings);
                    return true;
                };
            system_services.control_iot_agent =
                [socket = options.privileged_broker_socket_path, remember, iot_agent_monitor,
                 iot_agent_settings_path](micropanel_touch::core::IotAgentControlAction action,
                                          std::string* diagnostic) {
                    const auto reply =
                        micropanel_touch::platform::PrivilegedBrokerClient::iot_agent_control(
                            socket, micropanel_touch::core::IotAgentControlOperation{action},
                            diagnostic);
                    if (!reply.ok) {
                        if (diagnostic != nullptr && !reply.message.empty()) {
                            *diagnostic = reply.message;
                        }
                        return false;
                    }
                    iot_agent_monitor->reset();
                    // Disconnect is remembered so the indicator can say
                    // "Disconnected" rather than "Agent not running".
                    if (auto settings = micropanel_touch::platform::load_iot_agent_settings(
                            iot_agent_settings_path);
                        settings.has_value()) {
                        settings->enabled =
                            action == micropanel_touch::core::IotAgentControlAction::start;
                        remember(*settings);
                    }
                    return true;
                };
        }

        starter_ui = std::make_unique<micropanel_touch::ui::StarterUi>(
            *starter_config, theme, event_queue, synthetic_touch.get(), synthetic_keypad.get(),
            frame_capture,
            [&wifi_scan_provider] { wifi_scan_provider.request_scan(); },
            [&network_provider] { network_provider.request_managed_ipv4_profile(); },
            options.static_ip_interface,
            network_apply_service
                ? micropanel_touch::ui::StarterUi::NetworkRequestCallback(
                      [&network_apply_service](
                          std::uint64_t request_id,
                          const micropanel_touch::core::NetworkOperation& operation,
                          std::string* diagnostic) {
                          return network_apply_service->start(request_id, operation, diagnostic);
                      })
                : nullptr,
            system_update_service
                ? micropanel_touch::ui::StarterUi::SystemUpdateRequestCallback(
                      [&system_update_service](
                          std::uint64_t request_id,
                          const micropanel_touch::core::SystemUpdateOperation& operation,
                          std::string* diagnostic) {
                          return system_update_service->start(request_id, operation, diagnostic);
                      })
                : nullptr,
            system_update_service
                ? micropanel_touch::ui::StarterUi::SystemUpdateCheckCallback(
                      [&system_update_service](std::uint64_t request_id, std::string* diagnostic) {
                          return system_update_service->check(request_id, diagnostic);
                      })
                : nullptr,
            [] { return system_update_status(); },
            // Factory reset is a single synchronous broker call: the engine
            // only writes a marker and reboots, so there is no progress to
            // stream and nothing to join on shutdown.
            options.privileged_broker_socket_path.empty()
                ? micropanel_touch::ui::StarterUi::FactoryResetRequestCallback(nullptr)
                : micropanel_touch::ui::StarterUi::FactoryResetRequestCallback(
                      [socket = options.privileged_broker_socket_path](std::string* diagnostic) {
                          const auto reply =
                              micropanel_touch::platform::PrivilegedBrokerClient::factory_reset(
                                  socket, diagnostic);
                          if (!reply.ok && diagnostic != nullptr && !reply.message.empty()) {
                              *diagnostic = reply.message;
                          }
                          return reply.ok;
                      }),
            [&action_service, &execution_context](std::uint64_t job_id) {
                if (!execution_context.has_value()) {
                    return false;
                }
                std::string diagnostic;
                auto action = micropanel_touch::core::ActionCompiler::compile_native(
                    "demo.simulated-flash", *execution_context, &diagnostic);
                if (!action.has_value()) {
                    std::cerr << "Action demo compilation failed: " << diagnostic << '\n';
                    return false;
                }
                return action_service.start(job_id, std::move(*action));
            },
            [&action_service] { action_service.cancel(); },
            [&action_service](std::uint64_t job_id) { action_service.refresh_progress(job_id); },
            [&theme, display](const std::string& requested, std::string* diagnostic) {
                return theme.activate(requested, display, diagnostic);
            },
            [&theme] { return theme.active_skin().name; },
            display_sleep.has_value()
                ? micropanel_touch::ui::StarterUi::DisplayStandbySettingsProvider(
                      [&display_standby_settings] {
                          return std::optional<micropanel_touch::platform::DisplayStandbySettings>(
                              display_standby_settings);
                      })
                : nullptr,
            display_sleep.has_value()
                ? micropanel_touch::ui::StarterUi::DisplayStandbySettingsApplyCallback(
                      [&display_standby_settings, &display_sleep, display_settings_path](
                          const micropanel_touch::platform::DisplayStandbySettings& requested,
                          std::string* diagnostic) {
                          if (display_settings_path.empty()) {
                              if (diagnostic != nullptr) {
                                  *diagnostic = "persistent display settings storage is unavailable";
                              }
                              return false;
                          }
                          if (!micropanel_touch::platform::save_display_standby_settings(
                                  display_settings_path, requested, diagnostic)) {
                              return false;
                          }
                          display_standby_settings = requested;
                          display_sleep->set_timeout(std::chrono::seconds(
                              requested.enabled ? requested.seconds : 0U));
                          return true;
                      })
                : nullptr,
            display_brightness_available
                ? micropanel_touch::ui::StarterUi::DisplayBrightnessSettingsProvider(
                      [&display_brightness_settings] {
                          return std::optional<micropanel_touch::platform::DisplayBrightnessSettings>(
                              display_brightness_settings);
                      })
                : nullptr,
            display_brightness_available
                ? micropanel_touch::ui::StarterUi::DisplayBrightnessPreviewCallback(
                      [display_backlight](
                          const micropanel_touch::platform::DisplayBrightnessSettings& requested,
                          std::string* diagnostic) {
                          return display_backlight != nullptr &&
                                 display_backlight->set_brightness_percent(requested.percent, diagnostic);
                      })
                : nullptr,
            display_brightness_available
                ? micropanel_touch::ui::StarterUi::DisplayBrightnessSettingsApplyCallback(
                      [&display_brightness_settings, display_backlight,
                       display_brightness_settings_path](
                          const micropanel_touch::platform::DisplayBrightnessSettings& requested,
                          std::string* diagnostic) {
                          if (display_backlight == nullptr || display_brightness_settings_path.empty()) {
                              if (diagnostic != nullptr) {
                                  *diagnostic = "persistent brightness storage is unavailable";
                              }
                              return false;
                          }
                          const auto previous = display_brightness_settings;
                          if (!display_backlight->set_brightness_percent(requested.percent, diagnostic)) {
                              return false;
                          }
                          if (!micropanel_touch::platform::save_display_brightness_settings(
                                  display_brightness_settings_path, requested, diagnostic)) {
                              std::string restore_diagnostic;
                              if (!display_backlight->set_brightness_percent(previous.percent,
                                                                            &restore_diagnostic) &&
                                  diagnostic != nullptr && !restore_diagnostic.empty()) {
                                  *diagnostic += "; unable to restore previous brightness: " +
                                                 restore_diagnostic;
                              }
                              return false;
                          }
                          display_brightness_settings = requested;
                          return true;
                      })
                : nullptr,
            !screen_lock_settings_path.empty()
                ? micropanel_touch::ui::StarterUi::ScreenLockSettingsProvider(
                      [&screen_lock_settings] {
                          return std::optional<micropanel_touch::platform::ScreenLockSettings>(
                              screen_lock_settings);
                      })
                : nullptr,
            !screen_lock_settings_path.empty()
                ? micropanel_touch::ui::StarterUi::ScreenLockSetPinCallback(
                      [&screen_lock_settings, screen_lock_settings_path](std::string_view pin,
                                                                         std::string* diagnostic) {
                          auto updated = screen_lock_settings;
                          if (!micropanel_touch::platform::set_screen_lock_pin(&updated, pin,
                                                                               diagnostic)) {
                              return false;
                          }
                          if (!micropanel_touch::platform::save_screen_lock_settings(
                                  screen_lock_settings_path, updated, diagnostic)) {
                              return false;
                          }
                          screen_lock_settings = updated;
                          return true;
                      })
                : nullptr,
            !screen_lock_settings_path.empty()
                ? micropanel_touch::ui::StarterUi::ScreenLockSetEnabledCallback(
                      [&screen_lock_settings, screen_lock_settings_path](bool enabled,
                                                                         std::string* diagnostic) {
                          if (enabled && !screen_lock_settings.configured) {
                              if (diagnostic != nullptr) {
                                  *diagnostic = "set a PIN first";
                              }
                              return false;
                          }
                          auto updated = screen_lock_settings;
                          updated.enabled = enabled;
                          if (!micropanel_touch::platform::save_screen_lock_settings(
                                  screen_lock_settings_path, updated, diagnostic)) {
                              return false;
                          }
                          screen_lock_settings = updated;
                          return true;
                      })
                : nullptr,
            !screen_lock_settings_path.empty()
                ? micropanel_touch::ui::StarterUi::ScreenLockVerifyPinCallback(
                      [&screen_lock_settings](std::string_view pin) {
                          return micropanel_touch::platform::verify_screen_lock_pin(
                              screen_lock_settings, pin);
                      })
                : nullptr,
            !screen_lock_settings_path.empty()
                ? micropanel_touch::ui::StarterUi::ScreenLockSessionCallback(
                      [&screen_lock_session_locked, &screen_lock_settings](bool locked) {
                          screen_lock_session_locked = locked && screen_lock_settings.enabled;
                      })
                : nullptr,
            touch_calibration_callback, touch_calibration_reset_callback, logical_to_native,
            system_services);
        starter_ui->start();
        if (screen_lock_session_locked) {
            starter_ui->show_screen_lock();
        }
    }
    if (!options.control_socket_path.empty()) {
        std::string control_diagnostic;
        if (!control_server.start(options.control_socket_path, &control_diagnostic)) {
            std::cerr << "Unable to start control socket: " << control_diagnostic << '\n';
            return EXIT_FAILURE;
        }
        std::cout << "Control socket enabled at " << options.control_socket_path << '\n';
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    const std::filesystem::path first_frame_marker = options.runtime_dir_path.empty()
        ? std::filesystem::path{}
        : std::filesystem::path(options.runtime_dir_path) / "first-frame-ready";
    bool first_frame_marked = false;
    const auto started = std::chrono::steady_clock::now();
    auto next_display_sleep_check = started;
    while (keep_running.load()) {
        // lv_linux_fbdev_create installs LVGL's monotonic tick callback.
        const unsigned int next_wakeup_ms = lv_timer_handler();
        if (!first_frame_marked && !first_frame_marker.empty()) {
            // A successful synchronous refresh is the earliest point at which
            // the candidate commit service may treat the HMI as visibly alive.
            // The per-service runtime directory is owned by this unprivileged
            // process, while the root commit unit only tests the marker.
            lv_refr_now(display);
            std::ofstream marker(first_frame_marker, std::ios::out | std::ios::trunc);
            marker << "ready\n";
            first_frame_marked = marker.good();
        }
        const auto now = std::chrono::steady_clock::now();
        if (display_sleep.has_value() && now >= next_display_sleep_check) {
            const auto inactive_time =
                std::chrono::milliseconds(lv_display_get_inactive_time(display));
            const bool sleep_inhibited = action_service.busy() ||
                (starter_ui != nullptr && starter_ui->inhibits_display_sleep());
            if (starter_ui != nullptr && display_sleep->should_sleep(inactive_time, sleep_inhibited)) {
                starter_ui->return_to_home();
                if (screen_lock_settings.enabled) {
                    screen_lock_session_locked = true;
                    // Build the gate while the panel is still illuminated.
                    // DisplaySleep consumes the later wake contact, so the
                    // following contact can reach only this PIN screen.
                    starter_ui->show_screen_lock();
                }
                // Flush the eventual wake target while the backlight is
                // still illuminated. The wake contact is always consumed.
                lv_refr_now(display);
            }
            std::string diagnostic;
            display_sleep->update(inactive_time, sleep_inhibited, &diagnostic);
            if (!diagnostic.empty()) {
                std::cerr << "Display sleep transition failed: " << diagnostic << '\n';
            }
            next_display_sleep_check = now + std::chrono::seconds(1);
        }
        if (options.run_seconds > 0U && now - started >=
                std::chrono::seconds(options.run_seconds)) {
            break;
        }
        const unsigned int sleep_ms = display_sleep.has_value() && display_sleep->sleeping()
            ? kSleepingTimerSleepMs
            : std::min(next_wakeup_ms, kMaximumTimerSleepMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    control_server.stop();
    starter_ui.reset();
    system_update_service.reset();
    network_apply_service.reset();
    wifi_scan_provider.stop();
    network_provider.stop();
    action_service.stop();
    return EXIT_SUCCESS;
}
