// What happens between one diagnostic and the next.
//
// The service runs one test at a time, which is right - two tests sharing an
// interface measure each other - but it makes the handover the interesting
// part. An operator who stops a slow speed test to run a ping is asking for
// the service to be free again, and "free eventually" is indistinguishable
// from broken when the next screen says a test is already running.
#include "core/UiEventQueue.h"
#include "platform/NetworkTestService.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <variant>

namespace {

using micropanel_touch::core::UiEventQueue;
using micropanel_touch::platform::NetworkTestService;

std::filesystem::path write_handler(const std::filesystem::path& directory,
                                    const std::string& body) {
    const std::filesystem::path path = directory / "handler";
    std::ofstream out(path);
    out << body;
    out.close();
    std::filesystem::permissions(path, std::filesystem::perms::owner_all);
    return path;
}

// Drain until a terminal verdict arrives, or the budget runs out.
bool wait_for_result(UiEventQueue& queue, std::string* message, bool* ok,
                     std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& event : queue.drain()) {
            if (auto* verdict =
                    std::get_if<micropanel_touch::core::NetworkTestResult>(&event.payload)) {
                *message = verdict->message;
                *ok = verdict->ok;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

}  // namespace

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("network-test-service-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);

    {
        // A test that would run far longer than anyone waits: the speed check
        // on a slow link, in miniature.
        const std::filesystem::path handler = write_handler(directory,
                                                            "#!/bin/sh\n"
                                                            "echo starting\n"
                                                            "sleep 120\n");
        UiEventQueue queue;
        NetworkTestService service(queue, handler);

        std::string diagnostic;
        assert(service.start(1U, NetworkTestService::Test::speed, "eth0", {}, &diagnostic));

        // One at a time, and it says so rather than starting a second.
        diagnostic.clear();
        assert(!service.start(2U, NetworkTestService::Test::ping, "eth0", {}, &diagnostic));
        assert(!diagnostic.empty() && "a refused start has to say why");

        // Stopping it has to free the service on a human timescale. A cancel
        // that lands eventually is the bug this test exists for: the operator
        // presses Stop, taps the next test, and is told one is already
        // running.
        const auto cancelled_at = std::chrono::steady_clock::now();
        service.cancel();

        std::string message;
        bool ok = true;
        assert(wait_for_result(queue, &message, &ok, std::chrono::seconds(10)) &&
               "cancelling produced no verdict");
        assert(!ok && "an abandoned run is not a successful one");
        assert(message == "Test cancelled.");

        // The next test starts, and starts promptly.
        diagnostic.clear();
        bool restarted = false;
        while (std::chrono::steady_clock::now() - cancelled_at < std::chrono::seconds(10)) {
            if (service.start(3U, NetworkTestService::Test::ping, "eth0", {}, &diagnostic)) {
                restarted = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cancelled_at);
        assert(restarted && "the service never freed up after a cancel");
        // Generous enough for a loaded build machine, tight enough to catch a
        // cancel that waits for the child's own timeout. The child here would
        // have run for two minutes.
        assert(elapsed < std::chrono::seconds(5) &&
               "the service took too long to free up after a cancel");
        std::cout << "network test service: freed " << elapsed.count() << " ms after cancel\n";
        service.stop();
    }

    {
        // A handler that ignores SIGTERM still has to be stopped: the panel
        // escalates rather than waiting on a child's good manners.
        const std::filesystem::path handler = write_handler(directory,
                                                            "#!/bin/sh\n"
                                                            "trap '' TERM\n"
                                                            "echo stubborn\n"
                                                            "sleep 120\n");
        UiEventQueue queue;
        NetworkTestService service(queue, handler);
        std::string diagnostic;
        assert(service.start(1U, NetworkTestService::Test::speed, "eth0", {}, &diagnostic));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto cancelled_at = std::chrono::steady_clock::now();
        service.cancel();
        std::string message;
        bool ok = true;
        assert(wait_for_result(queue, &message, &ok, std::chrono::seconds(15)) &&
               "a TERM-ignoring handler was never stopped");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cancelled_at);
        assert(elapsed < std::chrono::seconds(10));
        std::cout << "network test service: stubborn handler stopped in " << elapsed.count()
                  << " ms\n";
        service.stop();
    }

    std::filesystem::remove_all(directory);
    std::cout << "network test service: cancel frees the service\n";
    return 0;
}
