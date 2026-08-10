#pragma once

#include "core/UiEventQueue.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace micropanel_touch::platform {

class NetworkInfoProvider {
public:
    explicit NetworkInfoProvider(core::UiEventQueue& event_queue);
    ~NetworkInfoProvider();
    NetworkInfoProvider(const NetworkInfoProvider&) = delete;
    NetworkInfoProvider& operator=(const NetworkInfoProvider&) = delete;

    void start();
    void stop();

    static core::NetworkSnapshot collect_snapshot();

private:
    void run();

    core::UiEventQueue& event_queue_;
    std::atomic_bool running_{false};
    std::thread worker_;
    std::uint64_t next_sequence_{1};
};

}  // namespace micropanel_touch::platform
