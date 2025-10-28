#pragma once

#include "iLock.h"
#include <atomic>
#include <thread>

namespace lt {

// TAS 自旋锁：使用普通原子变量作为状态，先 load 观察，再 CAS 抢占，减少不必要的总线锁冲突
// state_: 0 = unlocked, 1 = locked
class TasSpinlock : public iLock {
public:
    TasSpinlock() = default;

    void lock() override {
        for (;;) {
            // 先看一眼，如果已经被占用则不做 TAS，直接自旋等待
            if (state_.load(std::memory_order_relaxed) != 0) {
                backoff();
                continue;
            }
            int expected = 0;
            // 只有在观测为 0 时才尝试 CAS 设置为 1
            if (state_.compare_exchange_weak(expected, 1,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                return; // 获得锁
            }
            // CAS 失败，继续自旋
            backoff();
        }
    }

    void unlock() override {
        state_.store(0, std::memory_order_release);
    }

private:
    static inline void backoff() {
        // 轻量让步以降低争用下的总线压力；可按需替换为平台 pause 指令
        std::this_thread::yield();
    }

    std::atomic<int> state_{0};
};

} // namespace lt
