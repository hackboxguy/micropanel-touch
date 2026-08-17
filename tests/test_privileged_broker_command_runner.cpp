#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/CommandRunner.h"
#include "platform/PrivilegedBroker.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

using micropanel_touch::core::DhcpOperation;
using micropanel_touch::core::PrivilegedOperation;
using micropanel_touch::core::PrivilegedOperationReply;
using micropanel_touch::platform::CommandRunner;
using micropanel_touch::platform::CommandStatus;

PrivilegedOperationReply run_handler(const PrivilegedOperation& operation,
                                     const std::atomic_bool& cancellation_requested,
                                     std::atomic_bool* slow_handler_started,
                                     std::atomic_bool* slow_handler_cancelled) {
    const auto* const dhcp = std::get_if<DhcpOperation>(&operation);
    assert(dhcp != nullptr);
    const bool slow = dhcp->interface_name == "slow0";
    if (slow) {
        slow_handler_started->store(true);
    }
    const auto result = CommandRunner::run(
        {"/bin/sh", {"-c", slow ? "sleep 5" : "exit 0"}, std::chrono::seconds(10), 1024U,
         std::chrono::milliseconds(100)},
        cancellation_requested);
    if (slow) {
        slow_handler_cancelled->store(result.status == CommandStatus::cancelled);
    }
    return result.status == CommandStatus::succeeded
        ? PrivilegedOperationReply{true, "DHCP configuration applied."}
        : PrivilegedOperationReply{false, "DHCP configuration was cancelled."};
}

}  // namespace

int main() {
    const auto socket_path = std::filesystem::temp_directory_path() /
                             ("micropanel-touch-broker-command-runner-" +
                              std::to_string(getpid()) + ".sock");
    std::atomic_bool slow_handler_started{false};
    std::atomic_bool slow_handler_cancelled{false};
    micropanel_touch::platform::PrivilegedBrokerServer server(
        [&slow_handler_started, &slow_handler_cancelled](
            const PrivilegedOperation& operation, const std::atomic_bool& cancellation_requested) {
            return run_handler(operation, cancellation_requested, &slow_handler_started,
                               &slow_handler_cancelled);
        });
    std::string diagnostic;
    assert(server.start(socket_path, getuid(), &diagnostic));

    const PrivilegedOperationReply immediate =
        micropanel_touch::platform::PrivilegedBrokerClient::apply_dhcp(socket_path, {"eth0"},
                                                                          &diagnostic);
    assert(immediate.ok);

    std::thread slow_client([&socket_path] {
        const PrivilegedOperationReply reply =
            micropanel_touch::platform::PrivilegedBrokerClient::apply_dhcp(socket_path, {"slow0"});
        assert(!reply.ok);
    });
    for (unsigned int attempt = 0U; attempt < 100U && !slow_handler_started.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(slow_handler_started.load());
    server.stop();
    slow_client.join();
    assert(slow_handler_cancelled.load());
    return 0;
}
