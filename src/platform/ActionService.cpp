#include "platform/ActionService.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <utility>

namespace micropanel_touch::platform {
namespace {

constexpr auto kProgressPeriod = std::chrono::milliseconds(250);

bool has_parent_traversal(const std::filesystem::path& path) {
    for (const std::filesystem::path& component : path) {
        if (component == "..") {
            return true;
        }
    }
    return false;
}

int open_managed_log(const std::filesystem::path& path) {
    if (!path.is_absolute() || path.filename().empty() || has_parent_traversal(path)) {
        return -1;
    }
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                                S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        return -1;
    }
    if (fchmod(descriptor, S_IRUSR | S_IWUSR) != 0) {
        close(descriptor);
        return -1;
    }
    return descriptor;
}

bool write_all(int descriptor, std::string_view text) {
    std::size_t written = 0U;
    while (written < text.size()) {
        const ssize_t count = write(descriptor, text.data() + written, text.size() - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

}  // namespace

struct ActionService::State {
    std::uint64_t job_id{0};
    core::ActionDefinition definition;
    int log_descriptor{-1};
    bool log_write_failed{false};
    std::string captured_log;
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point last_progress_at{};

    ~State() {
        if (log_descriptor >= 0) {
            close(log_descriptor);
        }
    }
};

ActionService::ActionService(CommandService& command_service, core::UiEventQueue& event_queue)
    : command_service_(command_service), event_queue_(event_queue) {}

ActionService::~ActionService() {
    stop();
}

bool ActionService::start(std::uint64_t job_id, ManagedActionRequest request) {
    if (job_id == 0U || request.command.executable.empty() || request.command.timeout.count() <= 0 ||
        (request.definition.log_file.empty() && request.managed_log_path.has_value()) ||
        (!request.definition.log_file.empty() && !request.managed_log_path.has_value())) {
        return false;
    }

    auto state = std::make_unique<State>();
    state->job_id = job_id;
    state->definition = std::move(request.definition);
    state->started_at = std::chrono::steady_clock::now();
    if (request.managed_log_path.has_value()) {
        state->log_descriptor = open_managed_log(*request.managed_log_path);
        if (state->log_descriptor < 0) {
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ != nullptr) {
            return false;
        }
        active_ = std::move(state);
    }

    CommandCallbacks callbacks;
    callbacks.on_output = [this, job_id](std::string_view output) { handle_output(job_id, output); };
    callbacks.on_completion = [this](const core::CommandCompletion& completion) {
        handle_completion(completion);
    };
    callbacks.publish_completion = false;
    if (!command_service_.start(job_id, std::move(request.command), std::move(callbacks))) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ != nullptr && active_->job_id == job_id) {
            active_.reset();
        }
        return false;
    }
    publish_progress(job_id, true);
    return true;
}

void ActionService::cancel() {
    bool has_active_action = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        has_active_action = active_ != nullptr;
    }
    if (has_active_action) {
        command_service_.cancel();
    }
}

void ActionService::refresh_progress(std::uint64_t job_id) {
    publish_progress(job_id, false);
}

void ActionService::stop() {
    cancel();
    command_service_.stop();
    std::lock_guard<std::mutex> lock(mutex_);
    active_.reset();
}

bool ActionService::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ != nullptr;
}

void ActionService::handle_output(std::uint64_t job_id, std::string_view output) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ == nullptr || active_->job_id != job_id) {
            return;
        }
        active_->captured_log.append(output.data(), output.size());
        if (active_->log_descriptor >= 0 && !active_->log_write_failed &&
            !write_all(active_->log_descriptor, output)) {
            active_->log_write_failed = true;
        }
    }
    publish_progress(job_id, false);
}

void ActionService::handle_completion(const core::CommandCompletion& completion) {
    core::ActionTerminal terminal;
    bool publish = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ == nullptr || active_->job_id != completion.job_id) {
            return;
        }

        if (active_->log_descriptor >= 0) {
            // Completion is derived from the bounded captured output, not a
            // durability promise for the diagnostic log. Avoid an fsync here:
            // it would make an action's UI latency depend on the data volume;
            // the production ExecutionContext can choose a stronger log
            // durability policy when its data partition is introduced.
            close(active_->log_descriptor);
            active_->log_descriptor = -1;
        }
        std::optional<std::string> configured_log;
        if (!active_->definition.log_file.empty() && !active_->log_write_failed) {
            configured_log = active_->captured_log;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - active_->started_at);
        terminal.job_id = completion.job_id;
        terminal.result = core::ActionRunner::evaluate(active_->definition, completion,
                                                       configured_log, elapsed);
        if (active_->log_write_failed) {
            terminal.result.diagnostic =
                "Managed log write failed; action outcome is unknown.";
        }
        active_.reset();
        publish = true;
    }
    if (publish) {
        event_queue_.push(core::UiEvent{next_sequence_.fetch_add(1U), std::move(terminal)});
    }
}

void ActionService::publish_progress(std::uint64_t job_id, bool force) {
    core::ActionProgressUpdate update;
    bool publish = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ == nullptr || active_->job_id != job_id) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!force && active_->last_progress_at.time_since_epoch().count() != 0 &&
            now - active_->last_progress_at < kProgressPeriod) {
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - active_->started_at);
        update.job_id = job_id;
        update.progress = core::ActionRunner::progress(active_->definition, active_->captured_log,
                                                       elapsed);
        // The initial empty-state event must not suppress the first useful
        // line of output; users should see the first parsed percentage
        // immediately, then output is coalesced to the panel-safe cadence.
        if (!force) {
            active_->last_progress_at = now;
        }
        publish = true;
    }
    if (publish) {
        event_queue_.push_latest(
            core::UiEvent{next_sequence_.fetch_add(1U), std::move(update)});
    }
}

}  // namespace micropanel_touch::platform
