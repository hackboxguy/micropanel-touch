#include "platform/DisplayBackend.h"
#include "platform/TouchInput.h"
#include "ui/HelloScreen.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdlib>
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
    std::string framebuffer;
    std::string input;
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
        << "  --no-input              Run without a touch device\n"
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
        } else if (argument == "--fbdev" || argument == "--input" || argument == "--run-seconds") {
            if (++index >= argc) {
                std::cerr << argument << " requires a value\n";
                return false;
            }
            const std::string value = argv[index];
            if (argument == "--fbdev") {
                options->framebuffer = value;
            } else if (argument == "--input") {
                options->input = value;
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

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (options.probe_only) {
        std::cout << micropanel_touch::platform::DisplayBackend::format_probe();
        print_touch_devices();
        return EXIT_SUCCESS;
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
        touch->set_display_size(lv_display_get_horizontal_resolution(display),
                                lv_display_get_vertical_resolution(display));
        touch->attach_to_lvgl();
        std::cout << "Using touch device " << touch->device().path << " (" << touch->device().name << ")\n";
    }

    micropanel_touch::ui::HelloScreen hello_screen;
    hello_screen.create();

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
    return EXIT_SUCCESS;
}
