#include "core/ActionCompiler.h"
#include "core/LegacyConfig.h"
#include "platform/ActionService.h"
#include "platform/CommandService.h"
#include "platform/ControlServer.h"
#include "platform/DisplayBackend.h"
#include "platform/FrameCapture.h"
#include "platform/NetworkInfo.h"
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

struct Options {
    bool probe_only{false};
    bool no_input{false};
    bool portrait{false};
    std::string validate_config_path;
    std::string legacy_config_path;
    std::string control_socket_path;
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
    return true;
}

void print_touch_devices() {
    const auto devices = micropanel_touch::platform::TouchInput::enumerate();
    std::cout << "Touch candidates: " << devices.size() << '\n';
    for (const auto& device : devices) {
        std::cout << "  " << device.path << " (" << device.name << ")"
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

std::optional<micropanel_touch::core::ExecutionContext> make_development_execution_context(
    const std::filesystem::path& config_path, const char* executable, std::string* diagnostic) {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path executable_path = fs::absolute(executable, error);
    if (error || executable_path.parent_path().parent_path().empty()) {
        *diagnostic = "Unable to resolve executable location for ExecutionContext";
        return std::nullopt;
    }
    const fs::path home = executable_path.parent_path().parent_path().lexically_normal();
    const fs::path data = home / ".runtime-data";
    const fs::path absolute_config_path = fs::absolute(config_path, error);
    if (error) {
        *diagnostic = "Unable to resolve config location for ExecutionContext";
        return std::nullopt;
    }
    micropanel_touch::core::ExecutionContext context{
        home,
        absolute_config_path.parent_path().lexically_normal(),
        data,
        data / "logs",
        data / "run",
    };
    if (!context.validate(diagnostic)) {
        return std::nullopt;
    }
    fs::create_directories(context.log_dir, error);
    if (error) {
        *diagnostic = "Unable to create development action log directory: " + error.message();
        return std::nullopt;
    }
    fs::permissions(context.log_dir, fs::perms::owner_all, fs::perm_options::replace, error);
    if (error) {
        *diagnostic = "Unable to protect development action log directory: " + error.message();
        return std::nullopt;
    }
    return context;
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

    std::unique_ptr<micropanel_touch::platform::TouchInput> touch;
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
        touch->set_display_size(lv_display_get_original_horizontal_resolution(display),
                                lv_display_get_original_vertical_resolution(display));
        touch->attach_to_lvgl();
        std::cout << "Using touch device " << touch->device().path << " (" << touch->device().name << ")\n";
    }

    micropanel_touch::core::UiEventQueue event_queue;
    micropanel_touch::platform::NetworkInfoProvider network_provider(event_queue);
    micropanel_touch::platform::WifiScanProvider wifi_scan_provider(event_queue);
    micropanel_touch::platform::CommandService action_command_service(event_queue);
    micropanel_touch::platform::ActionService action_service(action_command_service, event_queue);
    micropanel_touch::platform::ControlServer control_server(
        event_queue, [framebuffer](std::string* diagnostic) {
            return micropanel_touch::platform::capture_framebuffer_rgb565(framebuffer, diagnostic);
        });
    std::unique_ptr<micropanel_touch::ui::LegacyUi> legacy_ui;
    std::unique_ptr<micropanel_touch::ui::StarterUi> starter_ui;
    std::optional<micropanel_touch::core::ExecutionContext> execution_context;
    if (use_legacy_config) {
        legacy_ui = std::make_unique<micropanel_touch::ui::LegacyUi>(*legacy_config, event_queue);
        legacy_ui->start();
    } else {
        std::string execution_context_diagnostic;
        execution_context =
            make_development_execution_context(config_path, argv[0], &execution_context_diagnostic);
        if (!execution_context.has_value()) {
            std::cerr << "Action demo is unavailable: " << execution_context_diagnostic << '\n';
        }
        network_provider.start();
        starter_ui = std::make_unique<micropanel_touch::ui::StarterUi>(
            *starter_config, theme, event_queue,
            [&wifi_scan_provider] { wifi_scan_provider.request_scan(); },
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
            [&theme] { return theme.active_skin().name; });
        starter_ui->start();
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
    const auto started = std::chrono::steady_clock::now();
    while (keep_running.load()) {
        // lv_linux_fbdev_create installs LVGL's monotonic tick callback.
        const unsigned int next_wakeup_ms = lv_timer_handler();
        if (options.run_seconds > 0U &&
            std::chrono::steady_clock::now() - started >=
                std::chrono::seconds(options.run_seconds)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::min(next_wakeup_ms, kMaximumTimerSleepMs)));
    }
    control_server.stop();
    wifi_scan_provider.stop();
    network_provider.stop();
    action_service.stop();
    return EXIT_SUCCESS;
}
