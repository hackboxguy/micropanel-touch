#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiEventQueue.h"

#include <cassert>
#include <thread>
#include <vector>

using micropanel_touch::core::NetworkSnapshot;
using micropanel_touch::core::UiEvent;
using micropanel_touch::core::UiEventQueue;

int main() {
    UiEventQueue queue;
    queue.push({1, NetworkSnapshot{}});
    queue.push({2, NetworkSnapshot{}});
    const auto latest = queue.drain();
    assert(latest.size() == 1U);
    assert(latest.front().sequence == 2U);

    std::vector<std::thread> producers;
    for (std::uint64_t producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&queue, producer] {
            for (std::uint64_t event = 0; event < 100; ++event) {
                queue.push({producer * 100 + event, NetworkSnapshot{}});
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    const auto events = queue.drain();
    assert(events.size() == 1U);
    assert(queue.drain().empty());
    return 0;
}
