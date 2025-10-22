#pragma once

#include "iLock.h"
#include <atomic>

namespace lt {

// Test-and-set spinlock using std::atomic_flag
class TasSpinlock : public iLock {
public:
    TasSpinlock() = default;

    void lock() override {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // busy-wait
        }
    }

    void unlock() override {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

} // namespace lt
