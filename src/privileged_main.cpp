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
#include <pwd.h>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <unistd.h>

namespace {

std::atomic_bool keep_running{true};

// The A/B update engine is board-agnostic and ships with the image from
// misc-tools/packages/pi-ab-update, not from this application's prefix, so it
// is resolved by absolute path rather than through resolve_handler().  The
// broker stays exactly what it was: the unprivileged-client boundary in front
// of a root-only CLI.
constexpr const char* kDefaultUpdateEngine = "/usr/local/sbin/ab-system-update";

struct Options {
    std::filesystem::path socket_path;
    uid_t allowed_uid{static_cast<uid_t>(-1)};
    std::string allowed_user;
    std::filesystem::path update_engine{kDefaultUpdateEngine};
};

void on_signal(int) {
    keep_running.store(false);
}

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " --socket /absolute/path (--allowed-uid UID | --allowed-user USER)"
                 " [--update-engine /absolute/path]\n";
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
        if ((argument == "--socket" || argument == "--allowed-uid" ||
             argument == "--allowed-user" || argument == "--update-engine") &&
            index + 1 < argc) {
            const std::string value = argv[++index];
            if (argument == "--socket") {
                options->socket_path = value;
            } else if (argument == "--allowed-uid") {
                if (!parse_uid(value, &options->allowed_uid)) {
                    return false;
                }
            } else if (argument == "--allowed-user") {
                options->allowed_user = value;
            } else if (argument == "--update-engine") {
                options->update_engine = value;
            }
        } else {
            return false;
        }
    }
    const bool has_allowed_uid = options->allowed_uid != static_cast<uid_t>(-1);
    const bool has_allowed_user = !options->allowed_user.empty();
    return options->socket_path.is_absolute() && options->update_engine.is_absolute() &&
           has_allowed_uid != has_allowed_user;
}

std::optional<uid_t> resolve_allowed_uid(const Options& options) {
    if (options.allowed_uid != static_cast<uid_t>(-1)) {
        return options.allowed_uid;
    }
    const passwd* const account = getpwnam(options.allowed_user.c_str());
    if (account == nullptr) {
        return std::nullopt;
    }
    return account->pw_uid;
}

std::optional<std::filesystem::path> resolve_handler(const std::string& handler_name) {
    namespace fs = std::filesystem;
    std::error_code error;
    // argv[0] is process input, while this root daemon's real executable is
    // supplied by procfs. Resolving the latter keeps handler discovery stable
    // even if a caller provides an unusual argv[0].
    const fs::path binary = fs::canonical("/proc/self/exe", error);
    if (error || binary.parent_path().empty()) {
        return std::nullopt;
    }
    const fs::path binary_directory = binary.parent_path();
    const bool installed_layout = binary_directory.filename() == "sbin" &&
                                  binary_directory.parent_path().filename() == "usr";
    const fs::path home = (installed_layout ? binary_directory.parent_path().parent_path()
                                             : binary_directory.parent_path())
                              .lexically_normal();
    const fs::path development_handler = home / "handlers" / handler_name;
    if (fs::is_regular_file(development_handler, error) && !error) {
        return development_handler;
    }
    // A missing source-tree handler is normal in the installed layout, where
    // handlers live under <prefix>/usr/bin.  Only a real inspection failure
    // (rather than ENOENT) prevents checking that installed location.
    if (error && error != std::errc::no_such_file_or_directory) {
        return std::nullopt;
    }
    error.clear();
    const fs::path installed_handler = home / "usr" / "bin" / handler_name;
    if (fs::is_regular_file(installed_handler, error) && !error) {
        return installed_handler;
    }
    return std::nullopt;
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
                       micropanel_touch::platform::kNetworkOperationTimeout,
                       16U * 1024U, std::chrono::milliseconds(1500)},
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

micropanel_touch::core::PrivilegedOperationReply apply_dhcp(
    const std::filesystem::path& handler, const micropanel_touch::core::DhcpOperation& operation,
    const std::atomic_bool& cancellation_requested) {
    using micropanel_touch::platform::CommandRequest;
    using micropanel_touch::platform::CommandResult;
    using micropanel_touch::platform::CommandRunner;
    using micropanel_touch::platform::CommandStatus;

    const CommandResult result = CommandRunner::run(
        CommandRequest{handler.string(), {operation.interface_name},
                       micropanel_touch::platform::kNetworkOperationTimeout,
                       16U * 1024U, std::chrono::milliseconds(1500)},
        cancellation_requested);
    if (result.status == CommandStatus::succeeded) {
        return {true, "DHCP configuration applied."};
    }
    if (result.status == CommandStatus::cancelled) {
        return {false, "DHCP configuration was cancelled."};
    }
    return {false, "DHCP configuration failed."};
}

micropanel_touch::core::PrivilegedOperationReply apply_dhcp_server(
    const std::filesystem::path& handler,
    const micropanel_touch::core::DhcpServerOperation& operation,
    const std::atomic_bool& cancellation_requested) {
    using micropanel_touch::platform::CommandRequest;
    using micropanel_touch::platform::CommandResult;
    using micropanel_touch::platform::CommandRunner;
    using micropanel_touch::platform::CommandStatus;

    const CommandResult result = CommandRunner::run(
        CommandRequest{handler.string(),
                       {operation.interface_name, operation.settings.address,
                        operation.settings.prefix_length, operation.settings.lease_start,
                        operation.settings.lease_end},
                       micropanel_touch::platform::kNetworkOperationTimeout,
                       16U * 1024U, std::chrono::milliseconds(1500)},
        cancellation_requested);
    if (result.status == CommandStatus::succeeded) {
        return {true, "DHCP server configuration applied."};
    }
    if (result.status == CommandStatus::cancelled) {
        return {false, "DHCP server configuration was cancelled."};
    }
    return {false, "DHCP server configuration failed."};
}

micropanel_touch::core::PrivilegedOperationReply apply_system_update(
    const std::filesystem::path& handler,
    const micropanel_touch::core::SystemUpdateOperation& operation,
    const std::atomic_bool& cancellation_requested) {
    using micropanel_touch::platform::CommandRequest;
    using micropanel_touch::platform::CommandResult;
    using micropanel_touch::platform::CommandRunner;
    using micropanel_touch::platform::CommandStatus;

    const CommandResult result = CommandRunner::run(
        CommandRequest{handler.string(), {operation.source},
                       micropanel_touch::platform::kSystemUpdateOperationTimeout,
                       16U * 1024U, std::chrono::seconds(5)},
        cancellation_requested);
    if (result.status == CommandStatus::succeeded) {
        return {true, "System update verified; rebooting into the candidate slot."};
    }
    if (result.status == CommandStatus::cancelled) {
        return {false, "System update was cancelled before candidate boot."};
    }
    // Handler output may contain device topology and release metadata. Keep
    // the broker reply bounded and safe for the UI/control protocol.
    return {false, "System update failed before candidate boot."};
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
    const auto allowed_uid = resolve_allowed_uid(options);
    if (!allowed_uid.has_value()) {
        std::cerr << "Unable to resolve allowed broker user\n";
        return EXIT_FAILURE;
    }
    const auto static_handler = resolve_handler("micropanel-touch-network-static-ip");
    const auto dhcp_handler = resolve_handler("micropanel-touch-network-dhcp");
    const auto dhcp_server_handler = resolve_handler("micropanel-touch-network-dhcp-server");
    if (!static_handler.has_value() || !dhcp_handler.has_value() ||
        !dhcp_server_handler.has_value()) {
        std::cerr << "Unable to resolve privileged handlers\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path update_handler = options.update_engine;

    micropanel_touch::platform::PrivilegedBrokerServer broker(
        [static_handler = *static_handler, dhcp_handler = *dhcp_handler,
         dhcp_server_handler = *dhcp_server_handler, update_handler = update_handler](
            const micropanel_touch::core::PrivilegedOperation& operation,
            const std::atomic_bool& cancellation_requested) {
            return std::visit([&](const auto& selected) {
                using Operation = std::decay_t<decltype(selected)>;
                if constexpr (std::is_same_v<Operation, micropanel_touch::core::StaticIpv4Operation>) {
                    return apply_static_ipv4(static_handler, selected, cancellation_requested);
                } else if constexpr (std::is_same_v<Operation,
                                                     micropanel_touch::core::DhcpOperation>) {
                    return apply_dhcp(dhcp_handler, selected, cancellation_requested);
                } else if constexpr (std::is_same_v<Operation,
                                                     micropanel_touch::core::DhcpServerOperation>) {
                    return apply_dhcp_server(dhcp_server_handler, selected, cancellation_requested);
                } else {
                    return apply_system_update(update_handler, selected, cancellation_requested);
                }
            }, operation);
        });
    std::string diagnostic;
    if (!broker.start(options.socket_path, *allowed_uid, &diagnostic)) {
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
