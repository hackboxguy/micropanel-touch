#include "platform/NetworkApplyService.h"

#include "platform/PrivilegedBroker.h"

#include <utility>
#include <type_traits>

namespace micropanel_touch::platform {

NetworkApplyService::NetworkApplyService(core::UiEventQueue& event_queue,
                                         std::filesystem::path broker_socket_path)
    : event_queue_(event_queue), broker_socket_path_(std::move(broker_socket_path)) {}

NetworkApplyService::~NetworkApplyService() {
    stop();
}

bool NetworkApplyService::start(std::uint64_t request_id,
                                const core::NetworkOperation& operation,
                                std::string* diagnostic) {
    if (request_id == 0U) {
        if (diagnostic != nullptr) {
            *diagnostic = "Network settings request has an invalid identifier.";
        }
        return false;
    }
    const core::StaticIpValidationResult validation = core::validate_network_operation(operation);
    if (!validation.valid) {
        if (diagnostic != nullptr) {
            *diagnostic = validation.message;
        }
        return false;
    }
    if (broker_socket_path_.empty()) {
        if (diagnostic != nullptr) {
            *diagnostic = "Network settings broker is not configured; no network changes were made.";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.exchange(true)) {
        if (diagnostic != nullptr) {
            *diagnostic = "A network settings request is already in progress.";
        }
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&NetworkApplyService::run, this, request_id, operation);
    return true;
}

void NetworkApplyService::stop() {
    // The current broker protocol has no client-side cancellation message.
    // Joining therefore waits for its bounded socket timeout, ensuring a
    // shutdown never leaves a UI worker accessing the event queue.
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.store(false);
        if (worker_.joinable()) {
            worker = std::move(worker_);
        }
    }
    if (worker.joinable()) {
        worker.join();
    }
}

void NetworkApplyService::run(std::uint64_t request_id, core::NetworkOperation operation) {
    std::string diagnostic;
    core::PrivilegedOperationReply reply = std::visit(
        [this, &diagnostic](const auto& selected) {
            using Operation = std::decay_t<decltype(selected)>;
            if constexpr (std::is_same_v<Operation, core::StaticIpv4Operation>) {
                return PrivilegedBrokerClient::apply_static_ipv4(broker_socket_path_, selected,
                                                                 &diagnostic);
            } else if constexpr (std::is_same_v<Operation, core::DhcpOperation>) {
                return PrivilegedBrokerClient::apply_dhcp(broker_socket_path_, selected, &diagnostic);
            } else {
                return PrivilegedBrokerClient::apply_dhcp_server(broker_socket_path_, selected,
                                                                  &diagnostic);
            }
        }, operation);
    if (reply.message.empty()) {
        reply.message = diagnostic.empty() ? "Network settings change failed." : diagnostic;
    }
    event_queue_.push(core::UiEvent{next_sequence_.fetch_add(1U),
                                    core::NetworkApplyResult{request_id, reply.ok,
                                                             std::move(reply.message)}});
    running_.store(false);
}

}  // namespace micropanel_touch::platform
