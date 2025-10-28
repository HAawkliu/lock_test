#pragma once

#include "iLock.h"
#include <atomic>

namespace lt {

// 原版 TAS 自旋锁：使用 atomic_flag 的 test_and_set
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

// 预观测版 TAS：先 load 观察，再 CAS 抢占，避免在已锁状态下进行原子写（RFO）
// state_: 0 = unlocked, 1 = locked
class TasSpinlockPreLoad : public iLock {
public:
    TasSpinlockPreLoad() = default;

    void lock() override {
        for (;;) {
            if (state_.load(std::memory_order_relaxed) != 0) {
                continue; // 已被占用，避免 RMW，继续自旋观察
            }
            int expected = 0;
            if (state_.compare_exchange_weak(expected, 1,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
                return;
            }
        }
    }

    void unlock() override {
        state_.store(0, std::memory_order_release);
    }

private:
    std::atomic<int> state_{0};
};

} // namespace lt
