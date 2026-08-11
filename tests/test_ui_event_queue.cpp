#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiEventQueue.h"

#include <cassert>
#include <thread>
#include <vector>

using micropanel_touch::core::NetworkSnapshot;
using micropanel_touch::core::ActionProgressUpdate;
using micropanel_touch::core::NetworkApplyResult;
using micropanel_touch::core::UiEvent;
using micropanel_touch::core::UiEventQueue;

int main() {
    UiEventQueue queue;
    queue.push({1, NetworkSnapshot{}});
    queue.push({2, NetworkSnapshot{}});
    const auto ordered = queue.drain();
    assert(ordered.size() == 2U);
    assert(ordered.front().sequence == 1U);
    assert(ordered.back().sequence == 2U);

    queue.push_latest({3, NetworkSnapshot{}});
    queue.push_latest({4, NetworkSnapshot{}});
    const auto latest = queue.drain();
    assert(latest.size() == 1U);
    assert(latest.front().sequence == 4U);

    queue.push({5, ActionProgressUpdate{11U, {10U, false, {"10%"}}}});
    queue.push_latest({6, ActionProgressUpdate{11U, {20U, false, {"20%"}}}});
    queue.push_latest({7, ActionProgressUpdate{11U, {30U, false, {"30%"}}}});
    const auto coalesced_action_progress = queue.drain();
    assert(coalesced_action_progress.size() == 1U);
    assert(coalesced_action_progress.front().sequence == 7U);
    const auto* action_progress =
        std::get_if<ActionProgressUpdate>(&coalesced_action_progress.front().payload);
    assert(action_progress != nullptr);
    assert(action_progress->progress.progress_percent == 30U);

    queue.push({8, NetworkApplyResult{21U, true, "Applied."}});
    queue.push({9, NetworkApplyResult{22U, false, "Rejected."}});
    const auto static_ip_results = queue.drain();
    assert(static_ip_results.size() == 2U);
    const auto* first_static_ip =
        std::get_if<NetworkApplyResult>(&static_ip_results.front().payload);
    const auto* second_static_ip =
        std::get_if<NetworkApplyResult>(&static_ip_results.back().payload);
    assert(first_static_ip != nullptr && first_static_ip->request_id == 21U && first_static_ip->ok);
    assert(second_static_ip != nullptr && second_static_ip->request_id == 22U && !second_static_ip->ok);

    std::vector<std::thread> producers;
    for (std::uint64_t producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&queue, producer] {
            for (std::uint64_t event = 0; event < 100; ++event) {
                queue.push_latest({producer * 100 + event, NetworkSnapshot{}});
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
