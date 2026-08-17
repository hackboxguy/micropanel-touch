#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiEventQueue.h"
#include "platform/PrivilegedBroker.h"
#include "platform/NetworkApplyService.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <unistd.h>

int main() {
    using micropanel_touch::core::NetworkApplyResult;
    using micropanel_touch::core::PrivilegedOperation;
    using micropanel_touch::core::StaticIpv4Operation;
    using micropanel_touch::core::UiEventQueue;

    const StaticIpv4Operation request{"eth0", {"192.168.1.20", "24", "192.168.1.1"}};
    UiEventQueue disabled_queue;
    micropanel_touch::platform::NetworkApplyService disabled(disabled_queue, {});
    std::string diagnostic;
    assert(!disabled.start(1U, request, &diagnostic));
    assert(diagnostic == "Network settings broker is not configured; no network changes were made.");
    assert(disabled_queue.drain().empty());

    const auto socket_path = std::filesystem::temp_directory_path() /
                             ("micropanel-touch-apply-" + std::to_string(getpid()) + ".sock");
    bool static_executed = false;
    bool dhcp_executed = false;
    bool dhcp_server_executed = false;
    micropanel_touch::platform::PrivilegedBrokerServer server(
        [&static_executed, &dhcp_executed, &dhcp_server_executed](const PrivilegedOperation& operation,
                                           const std::atomic_bool& cancellation_requested) {
            assert(!cancellation_requested.load());
            const auto* static_operation = std::get_if<StaticIpv4Operation>(&operation);
            if (static_operation != nullptr) {
                assert(static_operation->interface_name == "eth0");
                static_executed = true;
                return micropanel_touch::core::PrivilegedOperationReply{true, "Static IPv4 applied."};
            }
            const auto* dhcp_operation = std::get_if<micropanel_touch::core::DhcpOperation>(&operation);
            if (dhcp_operation != nullptr) {
                assert(dhcp_operation->interface_name == "eth0");
                dhcp_executed = true;
                return micropanel_touch::core::PrivilegedOperationReply{true, "DHCP applied."};
            }
            const auto* dhcp_server_operation =
                std::get_if<micropanel_touch::core::DhcpServerOperation>(&operation);
            assert(dhcp_server_operation != nullptr);
            assert(dhcp_server_operation->interface_name == "eth0");
            dhcp_server_executed = true;
            return micropanel_touch::core::PrivilegedOperationReply{true, "DHCP server applied."};
        });
    assert(server.start(socket_path, getuid(), &diagnostic));

    UiEventQueue queue;
    micropanel_touch::platform::NetworkApplyService service(queue, socket_path);
    assert(service.start(27U, request, &diagnostic));

    bool received = false;
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
        for (const auto& event : queue.drain()) {
            const auto* result = std::get_if<NetworkApplyResult>(&event.payload);
            if (result != nullptr) {
                assert(result->request_id == 27U);
                assert(result->ok);
                assert(result->message == "Static IPv4 applied.");
                received = true;
            }
        }
        if (received) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(received);
    assert(static_executed);

    assert(service.start(28U, micropanel_touch::core::DhcpOperation{"eth0"}, &diagnostic));
    bool dhcp_received = false;
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
        for (const auto& event : queue.drain()) {
            const auto* result = std::get_if<NetworkApplyResult>(&event.payload);
            if (result != nullptr) {
                assert(result->request_id == 28U);
                assert(result->ok);
                assert(result->message == "DHCP applied.");
                dhcp_received = true;
            }
        }
        if (dhcp_received) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(dhcp_received);
    assert(dhcp_executed);

    const micropanel_touch::core::DhcpServerOperation server_request{
        "eth0", {"192.168.50.1", "24", "192.168.50.100", "192.168.50.200"}};
    assert(service.start(29U, server_request, &diagnostic));
    bool dhcp_server_received = false;
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
        for (const auto& event : queue.drain()) {
            const auto* result = std::get_if<NetworkApplyResult>(&event.payload);
            if (result != nullptr) {
                assert(result->request_id == 29U);
                assert(result->ok);
                assert(result->message == "DHCP server applied.");
                dhcp_server_received = true;
            }
        }
        if (dhcp_server_received) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(dhcp_server_received);
    assert(dhcp_server_executed);
    service.stop();
    server.stop();
    return 0;
}
