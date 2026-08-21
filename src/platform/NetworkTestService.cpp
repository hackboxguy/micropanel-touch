#include "platform/NetworkTestService.h"

#include "platform/CommandRunner.h"

#include <chrono>
#include <utility>

namespace micropanel_touch::platform {
namespace {

// The backstop for a test that wedges. Each handler bounds its own work, so
// these only have to be longer than the work can legitimately take - and one
// number cannot cover all of them: a 100 MiB download on a slow link takes
// minutes, and a server is supposed to run until someone stops it.
std::chrono::seconds timeout_for(NetworkTestService::Test test) {
    switch (test) {
    case NetworkTestService::Test::speed:
        // 100 MiB is a real download. At 5 Mbit/s it is nearly three minutes,
        // and cutting it off would report a slow link as a broken one.
        return std::chrono::seconds{300};
    case NetworkTestService::Test::iperf_client:
        // The handler caps the run at sixty seconds; this leaves room for the
        // connection setup either side of it.
        return std::chrono::seconds{180};
    case NetworkTestService::Test::iperf_server:
        // Until it is stopped. A server killed by a timer would look like a
        // client problem, and the Stop button is the intended way out.
        return std::chrono::hours{24};
    default:
        return std::chrono::seconds{60};
    }
}

// Output is bounded so a chatty test cannot grow without limit, but the cap
// terminates the child when it is hit - which for a server means being killed
// for talking. It gets a larger allowance for the same reason it gets a longer
// timeout.
std::size_t output_limit_for(NetworkTestService::Test test) {
    return test == NetworkTestService::Test::iperf_server ? 512U * 1024U : 16U * 1024U;
}

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
    // Do not join here: cancel() is called from the UI thread and the worker
    // has to reach its terminal event first. start() joins the finished worker
    // before beginning the next one.
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
        CommandRequest{handler_path_.string(), std::move(arguments), timeout_for(test),
                       output_limit_for(test), std::chrono::milliseconds(1500)},
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
