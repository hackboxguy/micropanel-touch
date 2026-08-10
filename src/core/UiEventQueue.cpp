#include "core/UiEventQueue.h"

namespace micropanel_touch::core {

void UiEventQueue::push(UiEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto existing = events_.rbegin(); existing != events_.rend(); ++existing) {
        if (existing->payload.index() == event.payload.index()) {
            *existing = std::move(event);
            return;
        }
    }
    events_.push_back(std::move(event));
}

std::vector<UiEvent> UiEventQueue::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UiEvent> drained;
    drained.reserve(events_.size());
    while (!events_.empty()) {
        drained.push_back(std::move(events_.front()));
        events_.pop_front();
    }
    return drained;
}

}  // namespace micropanel_touch::core
