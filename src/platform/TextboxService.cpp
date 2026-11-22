#include "platform/TextboxService.h"

#include <string>
#include <utility>

namespace micropanel_touch::platform {

TextboxService::TextboxService(core::UiEventQueue& event_queue) : event_queue_(event_queue) {}

TextboxService::~TextboxService() {
    stop();
}

bool TextboxService::request(std::uint64_t request_id, CommandRequest request) {
    if (request.executable.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_) {
        return false;
    }
    // The previous worker has finished (busy_ is false), so this join is
    // immediate; it only reclaims the thread object.
    if (worker_.joinable()) {
        worker_.join();
    }
    cancel_ = false;
    busy_ = true;
    worker_ = std::thread(&TextboxService::run, this, request_id, std::move(request));
    return true;
}

void TextboxService::run(std::uint64_t request_id, CommandRequest request) {
    const CommandResult result = CommandRunner::run(request, cancel_);
    core::TextboxOutput output;
    output.request_id = request_id;
    output.ok = result.status == CommandStatus::succeeded;
    output.output = result.output;
    switch (result.status) {
    case CommandStatus::succeeded:
        break;
    case CommandStatus::failed:
        output.diagnostic = "exit status " + std::to_string(result.exit_status);
        break;
    case CommandStatus::timed_out:
        output.diagnostic = "timed out";
        break;
    case CommandStatus::output_limit_exceeded:
        output.diagnostic = "too much output";
        break;
    case CommandStatus::start_failed:
        output.diagnostic = "cannot start " + request.executable;
        break;
    case CommandStatus::killed:
    case CommandStatus::cancelled:
        output.diagnostic = "stopped";
        break;
    }
    std::uint64_t sequence = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sequence = next_sequence_++;
        busy_ = false;
    }
    if (!cancel_) {
        event_queue_.push(core::UiEvent{sequence, std::move(output)});
    }
}

void TextboxService::stop() {
    cancel_ = true;
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        worker = std::move(worker_);
    }
    if (worker.joinable()) {
        worker.join();
    }
}

bool TextboxService::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return busy_;
}

}  // namespace micropanel_touch::platform
