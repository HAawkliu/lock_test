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

// 预观测版 TicketLock：先观察是否空闲（serving == next），只有空闲时才 CAS 递增 next
// 注意：该版本不维护严格的 FIFO 排队，可能牺牲公平性以减少已锁状态下的原子写入
class TicketLockPreLoad : public iLock {
public:
    TicketLockPreLoad() = default;

    void lock() override {
        for (;;) {
            std::uint32_t s = serving_.load(std::memory_order_relaxed);
            std::uint32_t n = next_.load(std::memory_order_relaxed);
            if (s == n) {
                // 尝试在空闲时通过 CAS 领取票据
                std::uint32_t expected = n;
                if (next_.compare_exchange_weak(expected, n + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
                    // 我们的票据是 n，等待轮到我们（通常立即成功）
                    while (serving_.load(std::memory_order_acquire) != n) {
                        // busy-wait
                    }
                    return;
                }
            } else {
                // 已有持有者/排队，避免写 next_，仅观察等待
                continue;
            }
        }
    }

    void unlock() override {
        serving_.fetch_add(1, std::memory_order_release);
    }

private:
    std::atomic<std::uint32_t> next_ {0};
    std::atomic<std::uint32_t> serving_ {0};
};

} // namespace lt
