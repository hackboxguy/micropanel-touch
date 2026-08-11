#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiEventQueue.h"
#include "platform/PrivilegedBroker.h"
#include "platform/StaticIpv4ApplyService.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <unistd.h>

int main() {
    using micropanel_touch::core::StaticIpv4ApplyResult;
    using micropanel_touch::core::StaticIpv4Operation;
    using micropanel_touch::core::UiEventQueue;

    const StaticIpv4Operation request{"eth0", {"192.168.1.20", "24", "192.168.1.1"}};
    UiEventQueue disabled_queue;
    micropanel_touch::platform::StaticIpv4ApplyService disabled(disabled_queue, {});
    std::string diagnostic;
    assert(!disabled.start(1U, request, &diagnostic));
    assert(diagnostic == "Static IP broker is not configured; no network changes were made.");
    assert(disabled_queue.drain().empty());

    const auto socket_path = std::filesystem::temp_directory_path() /
                             ("micropanel-touch-apply-" + std::to_string(getpid()) + ".sock");
    micropanel_touch::platform::PrivilegedBrokerServer server(
        [](const StaticIpv4Operation& operation, const std::atomic_bool&) {
            assert(operation.interface_name == "eth0");
            return micropanel_touch::core::PrivilegedOperationReply{true, "Static IPv4 applied."};
        });
    assert(server.start(socket_path, getuid(), &diagnostic));

    UiEventQueue queue;
    micropanel_touch::platform::StaticIpv4ApplyService service(queue, socket_path);
    assert(service.start(27U, request, &diagnostic));

    bool received = false;
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
        for (const auto& event : queue.drain()) {
            const auto* result = std::get_if<StaticIpv4ApplyResult>(&event.payload);
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
    service.stop();
    server.stop();
    return 0;
}
