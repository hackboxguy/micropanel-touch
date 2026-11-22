#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiEventQueue.h"
#include "platform/TextboxService.h"

#include <cassert>
#include <chrono>
#include <thread>

using micropanel_touch::core::TextboxOutput;
using micropanel_touch::core::UiEventQueue;
using micropanel_touch::platform::CommandRequest;
using micropanel_touch::platform::TextboxService;

namespace {

TextboxOutput wait_for(UiEventQueue& queue, std::uint64_t id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& event : queue.drain()) {
            if (auto* output = std::get_if<TextboxOutput>(&event.payload)) {
                if (output->request_id == id) {
                    return *output;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(false && "timed out waiting for textbox output");
    return {};
}

}  // namespace

int main() {
    UiEventQueue queue;
    TextboxService service(queue);

    assert(service.request(1U, {"/bin/printf", {"Inside\\t10.6 C\\n!Cat\\taway\\n"}}));
    const TextboxOutput good = wait_for(queue, 1U);
    assert(good.ok);
    assert(good.output == "Inside\t10.6 C\n!Cat\taway\n");

    assert(service.request(2U, {"/bin/sh", {"-c", "echo partial; exit 3"}}));
    const TextboxOutput failed = wait_for(queue, 2U);
    assert(!failed.ok && failed.output == "partial\n" && failed.diagnostic == "exit status 3");

    // One run at a time: a tick while the script still runs is skipped.
    assert(service.request(3U, {"/bin/sleep", {"1"}}));
    assert(service.busy());
    assert(!service.request(4U, {"/bin/true", {}}));
    assert(wait_for(queue, 3U).ok);

    CommandRequest slow{"/bin/sleep", {"5"}};
    slow.timeout = std::chrono::milliseconds(200);
    assert(service.request(5U, slow));
    const TextboxOutput timed_out = wait_for(queue, 5U);
    assert(!timed_out.ok && timed_out.diagnostic == "timed out");

    assert(service.request(6U, {"/nonexistent/script", {}}));
    assert(!wait_for(queue, 6U).ok);

    // stop() cancels a live run and joins it promptly.
    assert(service.request(7U, {"/bin/sleep", {"10"}}));
    const auto started = std::chrono::steady_clock::now();
    service.stop();
    assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(4));
    assert(!service.busy());
    return 0;
}
