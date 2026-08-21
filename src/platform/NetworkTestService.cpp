#include "platform/NetworkTestService.h"

#include "platform/CommandRunner.h"

#include <chrono>
#include <utility>

namespace micropanel_touch::platform {
namespace {

// Long enough for the staged internet check to finish its five stages, short
// enough that a wedged test cannot hold the screen forever. The handler bounds
// each stage itself; this is the backstop.
constexpr std::chrono::seconds kNetworkTestTimeout{60};
constexpr std::size_t kMaximumOutputBytes = 16U * 1024U;

}  // namespace

std::string_view NetworkTestService::test_name(Test test) {
    switch (test) {
    case Test::ping:
        return "ping";
    case Test::internet:
        return "internet";
    case Test::speed:
        return "speed";
    case Test::neighbours:
        return "neighbours";
    case Test::port:
        return "port";
    case Test::iperf_server:
        return "iperf-server";
    case Test::iperf_client:
        return "iperf-client";
    case Test::iperf_discover:
        return "iperf-discover";
    }
    return "ping";
}

NetworkTestService::NetworkTestService(core::UiEventQueue& event_queue,
                                       std::filesystem::path handler_path)
    : event_queue_(event_queue), handler_path_(std::move(handler_path)) {}

NetworkTestService::~NetworkTestService() {
    stop();
}

bool NetworkTestService::start(std::uint64_t request_id, Test test,
                               const std::string& interface_name,
                               std::vector<std::string> arguments, std::string* diagnostic) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) {
        if (diagnostic != nullptr) {
            *diagnostic = "A network test is already running.";
        }
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    cancellation_requested_.store(false);
    running_.store(true);
    worker_ = std::thread(&NetworkTestService::run, this, request_id, test, interface_name,
                          std::move(arguments));
    return true;
}

void NetworkTestService::cancel() {
    cancellation_requested_.store(true);
}

void NetworkTestService::stop() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation_requested_.store(true);
        if (worker_.joinable()) {
            worker = std::move(worker_);
        }
    }
    if (worker.joinable()) {
        worker.join();
    }
}

void NetworkTestService::run(std::uint64_t request_id, Test test, std::string interface_name,
                             std::vector<std::string> extra) {
    std::vector<std::string> arguments{std::string(test_name(test)), std::move(interface_name)};
    for (std::string& argument : extra) {
        arguments.push_back(std::move(argument));
    }

    // Streamed rather than buffered: a staged check that shows nothing for ten
    // seconds and then everything is indistinguishable from one that hung.
    const CommandResult result = CommandRunner::run(
        CommandRequest{handler_path_.string(), std::move(arguments), kNetworkTestTimeout,
                       kMaximumOutputBytes, std::chrono::milliseconds(1500)},
        cancellation_requested_,
        [this, request_id](std::string_view output) {
            event_queue_.push({next_sequence_.fetch_add(1U),
                               core::NetworkTestOutput{request_id, std::string(output)}});
        });

    std::string message;
    bool ok = false;
    switch (result.status) {
    case CommandStatus::succeeded:
        ok = true;
        message = "Test finished.";
        break;
    case CommandStatus::cancelled:
        // Stopping a server is how a server ends. Only the finite tests treat
        // cancellation as an abandoned run.
        ok = test == Test::iperf_server;
        message = test == Test::iperf_server ? "Server stopped." : "Test cancelled.";
        break;
    case CommandStatus::timed_out:
        message = "Test timed out.";
        break;
    case CommandStatus::output_limit_exceeded:
        message = "Test produced too much output.";
        break;
    case CommandStatus::start_failed:
        message = "This panel cannot run network tests.";
        break;
    default:
        message = "Test failed.";
        break;
    }
    event_queue_.push({next_sequence_.fetch_add(1U),
                       core::NetworkTestResult{request_id, ok, std::move(message)}});
    running_.store(false);
}

}  // namespace micropanel_touch::platform
