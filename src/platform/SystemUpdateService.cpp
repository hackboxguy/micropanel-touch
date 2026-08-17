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

std::optional<std::string_view> failure_message_for_phase(std::string_view phase) {
    if (phase == "failed-source") {
        return "USB update media was not found or could not be mounted. Check its MP_UPDATE label.";
    }
    if (phase == "failed-compatibility") {
        return "This update is for a different panel image or Raspberry Pi board.";
    }
    if (phase == "failed-payload") {
        return "The USB stick does not contain one complete, valid update payload.";
    }
    if (phase == "failed-integrity") {
        return "The update payload failed its integrity check; no candidate boot was armed.";
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
