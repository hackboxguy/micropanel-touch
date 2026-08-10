#include "platform/CommandRunner.h"
#include "platform/PrivilegedBroker.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

std::atomic_bool keep_running{true};

struct Options {
    std::filesystem::path socket_path;
    uid_t allowed_uid{static_cast<uid_t>(-1)};
};

void on_signal(int) {
    keep_running.store(false);
}

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " --socket /absolute/path --allowed-uid UID\n";
}

bool parse_uid(const std::string& text, uid_t* uid) {
    unsigned long long value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value > std::numeric_limits<uid_t>::max()) {
        return false;
    }
    *uid = static_cast<uid_t>(value);
    return true;
}

bool parse_options(int argc, char* argv[], Options* options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--socket" || argument == "--allowed-uid") && index + 1 < argc) {
            const std::string value = argv[++index];
            if (argument == "--socket") {
                options->socket_path = value;
            } else if (!parse_uid(value, &options->allowed_uid)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return options->socket_path.is_absolute() && options->allowed_uid != static_cast<uid_t>(-1);
}

std::optional<std::filesystem::path> resolve_static_ip_handler(const char* executable) {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path binary = fs::weakly_canonical(fs::absolute(executable, error), error);
    if (error || binary.parent_path().empty()) {
        return std::nullopt;
    }
    const fs::path binary_directory = binary.parent_path();
    const bool installed_layout = binary_directory.filename() == "sbin" &&
                                  binary_directory.parent_path().filename() == "usr";
    const fs::path home = (installed_layout ? binary_directory.parent_path().parent_path()
                                             : binary_directory.parent_path())
                              .lexically_normal();
    const fs::path development_handler = home / "handlers" / "micropanel-touch-network-static-ip";
    if (fs::is_regular_file(development_handler, error) && !error) {
        return development_handler;
    }
    if (error) {
        return std::nullopt;
    }
    return home / "usr" / "bin" / "micropanel-touch-network-static-ip";
}

micropanel_touch::core::PrivilegedOperationReply apply_static_ipv4(
    const std::filesystem::path& handler,
    const micropanel_touch::core::StaticIpv4Operation& operation,
    const std::atomic_bool& cancellation_requested) {
    using micropanel_touch::platform::CommandRequest;
    using micropanel_touch::platform::CommandResult;
    using micropanel_touch::platform::CommandRunner;
    using micropanel_touch::platform::CommandStatus;

    const CommandResult result = CommandRunner::run(
        CommandRequest{handler.string(),
                       {operation.interface_name, operation.settings.address,
                        operation.settings.prefix_length, operation.settings.gateway},
                       std::chrono::seconds(45), 16U * 1024U, std::chrono::milliseconds(1500)},
        cancellation_requested);
    if (result.status == CommandStatus::succeeded) {
        return {true, "Static IPv4 configuration applied."};
    }
    if (result.status == CommandStatus::cancelled) {
        return {false, "Static IPv4 configuration was cancelled."};
    }
    // Do not return command output: it may contain connection-profile details
    // and must not become an unbounded privileged-protocol diagnostic.
    return {false, "Static IPv4 configuration failed."};
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (geteuid() != 0) {
        std::cerr << "micropanel-touch-privileged must run as root\n";
        return EXIT_FAILURE;
    }
    const auto handler = resolve_static_ip_handler(argv[0]);
    if (!handler.has_value()) {
        std::cerr << "Unable to resolve the static IPv4 handler\n";
        return EXIT_FAILURE;
    }

    micropanel_touch::platform::PrivilegedBrokerServer broker(
        [handler = *handler](const micropanel_touch::core::StaticIpv4Operation& operation,
                             const std::atomic_bool& cancellation_requested) {
            return apply_static_ipv4(handler, operation, cancellation_requested);
        });
    std::string diagnostic;
    if (!broker.start(options.socket_path, options.allowed_uid, &diagnostic)) {
        std::cerr << "Unable to start privileged broker: " << diagnostic << '\n';
        return EXIT_FAILURE;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    while (keep_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    broker.stop();
    return EXIT_SUCCESS;
}
