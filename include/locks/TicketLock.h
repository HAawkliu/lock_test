#pragma once

#include "iLock.h"
#include <atomic>
#include <cstdint>

namespace lt {

// Fair ticket lock: each thread acquires a ticket and waits for its turn.
class TicketLock : public iLock {
public:
    TicketLock() = default;

    void lock() override {
        // Fetch our ticket number
        const std::uint32_t my = next_.fetch_add(1, std::memory_order_relaxed);
        // Spin until serving equals our ticket
        while (serving_.load(std::memory_order_acquire) != my) {
            // busy-wait; optional pause could be added here
        }
    }

    void unlock() override {
        // Advance to next ticket
        serving_.fetch_add(1, std::memory_order_release);
    }

private:
    std::atomic<std::uint32_t> next_ {0};
    std::atomic<std::uint32_t> serving_ {0};
};

} // namespace lt
