#include "platform/CommandService.h"

#include <utility>

namespace micropanel_touch::platform {
namespace {

core::CommandCompletionStatus completion_status(CommandStatus status) {
    switch (status) {
        case CommandStatus::succeeded:
            return core::CommandCompletionStatus::Succeeded;
        case CommandStatus::failed:
            return core::CommandCompletionStatus::Failed;
        case CommandStatus::timed_out:
            return core::CommandCompletionStatus::TimedOut;
        case CommandStatus::cancelled:
            return core::CommandCompletionStatus::Cancelled;
        case CommandStatus::output_limit_exceeded:
            return core::CommandCompletionStatus::OutputLimitExceeded;
        case CommandStatus::start_failed:
            return core::CommandCompletionStatus::StartFailed;
    }
    return core::CommandCompletionStatus::StartFailed;
}

}  // namespace

CommandService::CommandService(core::UiEventQueue& event_queue) : event_queue_(event_queue) {}

CommandService::~CommandService() {
    stop();
}

bool CommandService::start(std::uint64_t job_id, CommandRequest request,
                           CommandCallbacks callbacks) {
    if (job_id == 0U || request.executable.empty() || request.timeout.count() <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_) {
        return false;
    }
    // A previous worker has already published its event and cleared busy_.
    // Joining it here keeps thread ownership bounded to this service.
    if (worker_.joinable()) {
        worker_.join();
    }
    cancellation_requested_.store(false);
    busy_ = true;
    worker_ = std::thread(&CommandService::run, this, next_sequence_++, job_id, std::move(request),
                          std::move(callbacks));
    return true;
}

void CommandService::cancel() {
    cancellation_requested_.store(true);
}

void CommandService::stop() {
    cancellation_requested_.store(true);
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.joinable()) {
            worker = std::move(worker_);
        }
    }
    if (worker.joinable()) {
        worker.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    busy_ = false;
}

bool CommandService::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return busy_;
}

void CommandService::run(std::uint64_t sequence, std::uint64_t job_id, CommandRequest request,
                         CommandCallbacks callbacks) {
    CommandResult result = CommandRunner::run(request, cancellation_requested_,
                                              std::move(callbacks.on_output));
    // Clear busy before posting the event. The UI can therefore start a new
    // job when it consumes this terminal event; start() joins this finished
    // worker before it owns the next one, preserving event order.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        busy_ = false;
    }
    core::CommandCompletion completion{job_id, completion_status(result.status), result.exit_status,
                                       std::move(result.output)};
    if (callbacks.on_completion) {
        callbacks.on_completion(completion);
    }
    event_queue_.push(core::UiEvent{sequence, std::move(completion)});
}

}  // namespace micropanel_touch::platform
