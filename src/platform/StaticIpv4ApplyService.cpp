#include "platform/StaticIpv4ApplyService.h"

#include "platform/PrivilegedBroker.h"

#include <utility>

namespace micropanel_touch::platform {

StaticIpv4ApplyService::StaticIpv4ApplyService(core::UiEventQueue& event_queue,
                                               std::filesystem::path broker_socket_path)
    : event_queue_(event_queue), broker_socket_path_(std::move(broker_socket_path)) {}

StaticIpv4ApplyService::~StaticIpv4ApplyService() {
    stop();
}

bool StaticIpv4ApplyService::start(std::uint64_t request_id,
                                   const core::StaticIpv4Operation& operation,
                                   std::string* diagnostic) {
    if (request_id == 0U) {
        if (diagnostic != nullptr) {
            *diagnostic = "Static IP request has an invalid identifier.";
        }
        return false;
    }
    const core::StaticIpValidationResult validation =
        core::validate_static_ipv4_operation(operation);
    if (!validation.valid) {
        if (diagnostic != nullptr) {
            *diagnostic = validation.message;
        }
        return false;
    }
    if (broker_socket_path_.empty()) {
        if (diagnostic != nullptr) {
            *diagnostic = "Static IP broker is not configured; no network changes were made.";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.exchange(true)) {
        if (diagnostic != nullptr) {
            *diagnostic = "A static IP request is already in progress.";
        }
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&StaticIpv4ApplyService::run, this, request_id, operation);
    return true;
}

void StaticIpv4ApplyService::stop() {
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

void StaticIpv4ApplyService::run(std::uint64_t request_id, core::StaticIpv4Operation operation) {
    std::string diagnostic;
    core::PrivilegedOperationReply reply = PrivilegedBrokerClient::apply_static_ipv4(
        broker_socket_path_, operation, &diagnostic);
    if (reply.message.empty()) {
        reply.message = diagnostic.empty() ? "Static IPv4 configuration failed." : diagnostic;
    }
    event_queue_.push(core::UiEvent{next_sequence_.fetch_add(1U),
                                    core::StaticIpv4ApplyResult{request_id, reply.ok,
                                                                 std::move(reply.message)}});
    running_.store(false);
}

}  // namespace micropanel_touch::platform
