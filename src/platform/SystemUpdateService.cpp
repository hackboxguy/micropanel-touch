#include "platform/SystemUpdateService.h"

#include "platform/PrivilegedBroker.h"

#include <charconv>
#include <chrono>
#include <fstream>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace micropanel_touch::platform {
namespace {

std::optional<core::SystemUpdateProgress> read_progress(std::uint64_t request_id) {
    // The handler publishes this root-owned, telemetry-only runtime file by
    // rename. Treat it as untrusted input nevertheless: bounded, exact, and
    // never used to make a privileged decision.
    std::ifstream input("/run/micropanel-touch-update/progress");
    std::string line;
    std::string phase;
    unsigned int percent = 0U;
    bool got_phase = false;
    bool got_percent = false;
    while (std::getline(input, line)) {
        if (line.size() > 128U) {
            return std::nullopt;
        }
        if (line.rfind("phase=", 0U) == 0U && !got_phase) {
            phase = line.substr(6U);
            got_phase = !phase.empty() && phase.size() <= 32U;
        } else if (line.rfind("progress=", 0U) == 0U && !got_percent) {
            const std::string value = line.substr(9U);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), percent);
            got_percent = parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                          percent <= 100U;
        } else {
            return std::nullopt;
        }
    }
    if (!got_phase || !got_percent) {
        return std::nullopt;
    }
    return core::SystemUpdateProgress{request_id, std::move(phase), percent};
}

// The published check state: `state=` and, when there is one, `version=`.
// Same discipline as the progress file - root-owned, telemetry only, and
// treated as untrusted input regardless.
struct PublishedCheck {
    std::string state;
    std::string version;
};

std::optional<PublishedCheck> read_check() {
    std::ifstream input("/run/micropanel-touch-update/check");
    std::string line;
    PublishedCheck published;
    bool got_state = false;
    while (std::getline(input, line)) {
        if (line.size() > 128U) {
            return std::nullopt;
        }
        if (line.rfind("state=", 0U) == 0U && !got_state) {
            published.state = line.substr(6U);
            got_state = !published.state.empty() && published.state.size() <= 32U;
        } else if (line.rfind("version=", 0U) == 0U && published.version.empty()) {
            published.version = line.substr(8U);
            if (published.version.size() > 64U) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    if (!got_state) {
        return std::nullopt;
    }
    return published;
}

std::optional<std::string_view> failure_message_for_check_state(std::string_view state) {
    if (state == "network") {
        return "The release server could not be reached.";
    }
    if (state == "clock") {
        // Distinguished from a plain network failure on purpose: this one is
        // fixed by giving the panel the time, not by fixing the network.
        return "The release server could not be authenticated because this panel's clock is "
               "not set. Connect it to a time source, or update from a USB stick.";
    }
    if (state == "signature") {
        return "The offered release is not signed by this panel's release key; it was ignored.";
    }
    if (state == "compatibility") {
        return "The offered release is for a different panel image or Raspberry Pi board.";
    }
    if (state == "payload") {
        return "The release server returned a release description this panel cannot read.";
    }
    if (state == "image") {
        return "This panel is not configured to fetch updates over the network.";
    }
    if (state == "internal") {
        return "The update check stopped safely; nothing was changed.";
    }
    return std::nullopt;
}

std::optional<std::string_view> failure_message_for_phase(std::string_view phase) {
    if (phase == "failed-source") {
        return "No USB stick with a readable FAT32 or exFAT filesystem was found.";
    }
    if (phase == "failed-compatibility") {
        return "This update is for a different panel image or Raspberry Pi board.";
    }
    if (phase == "failed-payload") {
        return "The USB stick must hold exactly one valid .mpupdate file.";
    }
    if (phase == "failed-version") {
        return "This panel already runs that software version; nothing was changed.";
    }
    if (phase == "failed-integrity") {
        return "The update payload failed its integrity check; no candidate boot was armed.";
    }
    if (phase == "failed-stall") {
        return "The update data stopped arriving; no candidate boot was armed.";
    }
    if (phase == "failed-boot") {
        return "The update boot files were refused; no candidate boot was armed.";
    }
    if (phase == "failed-target") {
        return "The inactive update slot is unavailable; no candidate boot was armed.";
    }
    if (phase == "failed-selector") {
        return "The A/B boot selector is unavailable; no candidate boot was armed.";
    }
    if (phase == "failed-image") {
        return "The running system is not prepared for an A/B update; no candidate boot was armed.";
    }
    if (phase == "failed-signature") {
        return "This update is not signed by this panel's release key; nothing was changed.";
    }
    if (phase == "failed-network") {
        return "The release download did not complete; no candidate boot was armed.";
    }
    if (phase == "failed-clock") {
        return "The release server could not be authenticated because this panel's clock is "
               "not set. Connect it to a time source, or update from a USB stick.";
    }
    if (phase == "failed-internal") {
        return "The update stopped safely before candidate boot.";
    }
    return std::nullopt;
}

}  // namespace

SystemUpdateService::SystemUpdateService(core::UiEventQueue& event_queue,
                                         std::filesystem::path broker_socket_path)
    : event_queue_(event_queue), broker_socket_path_(std::move(broker_socket_path)) {}

SystemUpdateService::~SystemUpdateService() {
    stop();
}

bool SystemUpdateService::start(std::uint64_t request_id,
                                const core::SystemUpdateOperation& operation,
                                std::string* diagnostic) {
    if (request_id == 0U) {
        if (diagnostic != nullptr) {
            *diagnostic = "System update request has an invalid identifier.";
        }
        return false;
    }
    const core::StaticIpValidationResult validation = core::validate_system_update_operation(operation);
    if (!validation.valid) {
        if (diagnostic != nullptr) {
            *diagnostic = validation.message;
        }
        return false;
    }
    if (broker_socket_path_.empty()) {
        if (diagnostic != nullptr) {
            *diagnostic = "System update broker is not configured; no update was started.";
        }
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.exchange(true)) {
        if (diagnostic != nullptr) {
            *diagnostic = "A system update is already in progress.";
        }
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&SystemUpdateService::run, this, request_id, operation);
    return true;
}

bool SystemUpdateService::check(std::uint64_t request_id, std::string* diagnostic) {
    if (request_id == 0U) {
        if (diagnostic != nullptr) {
            *diagnostic = "Update check request has an invalid identifier.";
        }
        return false;
    }
    if (broker_socket_path_.empty()) {
        if (diagnostic != nullptr) {
            *diagnostic = "System update broker is not configured; no check was started.";
        }
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // One worker for both operations: checking while a candidate slot is being
    // written would compete for the same engine lock and tell the operator
    // nothing they can act on until the install finishes.
    if (running_.exchange(true)) {
        if (diagnostic != nullptr) {
            *diagnostic = "A system update is already in progress.";
        }
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&SystemUpdateService::run_check, this, request_id);
    return true;
}

void SystemUpdateService::run_check(std::uint64_t request_id) {
    std::string diagnostic;
    const core::PrivilegedOperationReply reply =
        PrivilegedBrokerClient::check_system_update(broker_socket_path_, &diagnostic);
    const auto published = read_check();

    core::SystemUpdateCheckResult result{request_id, false, false, {}, {}};
    if (reply.ok && published.has_value() && published->state == "available") {
        result.ok = true;
        result.update_available = true;
        result.version = published->version;
        result.message = "Update available: " + published->version;
    } else if (reply.ok && published.has_value() && published->state == "up-to-date") {
        result.ok = true;
        result.message = published->version.empty()
                             ? std::string{"This panel is up to date."}
                             : "This panel is up to date (" + published->version + ").";
    } else if (published.has_value()) {
        if (const auto message = failure_message_for_check_state(published->state);
            message.has_value()) {
            result.message = std::string{*message};
        }
    }
    if (result.message.empty()) {
        result.message = reply.message.empty()
                             ? (diagnostic.empty() ? std::string{"The update check failed."}
                                                   : diagnostic)
                             : reply.message;
    }
    event_queue_.push(core::UiEvent{next_sequence_.fetch_add(1U), result});
    running_.store(false);
}

void SystemUpdateService::stop() {
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

void SystemUpdateService::run(std::uint64_t request_id, core::SystemUpdateOperation operation) {
    std::atomic_bool completed{false};
    std::thread progress_monitor([this, request_id, &completed] {
        std::string previous;
        while (!completed.load()) {
            if (const auto progress = read_progress(request_id); progress.has_value()) {
                const std::string fingerprint = progress->phase + ":" +
                                                std::to_string(progress->percent);
                if (fingerprint != previous) {
                    event_queue_.push(core::UiEvent{next_sequence_.fetch_add(1U), *progress});
                    previous = fingerprint;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });
    std::string diagnostic;
    core::PrivilegedOperationReply reply =
        PrivilegedBrokerClient::apply_system_update(broker_socket_path_, operation, &diagnostic);
    if (!reply.ok) {
        if (const auto progress = read_progress(request_id); progress.has_value()) {
            if (const auto message = failure_message_for_phase(progress->phase); message.has_value()) {
                reply.message = std::string{*message};
            }
        }
    }
    if (reply.message.empty()) {
        reply.message = diagnostic.empty() ? "System update failed before candidate boot." : diagnostic;
    }
    completed.store(true);
    progress_monitor.join();
    event_queue_.push(core::UiEvent{next_sequence_.fetch_add(1U),
                                    core::SystemUpdateResult{request_id, reply.ok,
                                                             std::move(reply.message)}});
    running_.store(false);
}

}  // namespace micropanel_touch::platform
