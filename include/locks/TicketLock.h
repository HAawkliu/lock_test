#pragma once

#include "iLock.h"
#include <atomic>
#include <cstdint>
#include <thread>

#if defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h>
#endif

namespace lt {

// Cache line size helper (fallback 64)
#if defined(__cpp_lib_hardware_interference_size)
constexpr std::size_t kTicketCacheLine = std::hardware_destructive_interference_size;
#else
constexpr std::size_t kTicketCacheLine = 64;
#endif

// Write prefetch hint (best-effort)
static inline void prefetchw(const void* p) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(p, 1, 3);
#else
    (void)p;
#endif
}

// CPU relax/backoff primitive
static inline void cpu_relax_once() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    // Prevent aggressive compiler reordering; very light-weight
    asm volatile("");
#endif
}

static inline void cpu_relax_n(unsigned n) {
    while (n--) cpu_relax_once();
}

// An aligned atomic wrapper to avoid false sharing by occupying its own cache line
template <typename T>
struct alignas(kTicketCacheLine) AlignedAtomic {
    std::atomic<T> v{};
    char pad[kTicketCacheLine - (sizeof(std::atomic<T>) % kTicketCacheLine ? sizeof(std::atomic<T>) % kTicketCacheLine : kTicketCacheLine)]{};
};

// Fair ticket lock: each thread acquires a ticket and waits for its turn.
class TicketLock : public iLock {
public:
    TicketLock() = default;

    void lock() override {
        // Fetch our ticket number
        const std::uint32_t my = next_.v.fetch_add(1, std::memory_order_relaxed);
        // Spin until serving equals our ticket
        while (serving_.v.load(std::memory_order_acquire) != my) {
            // busy-wait; optional pause could be added here
        }
    }

    void unlock() override {
        // Advance to next ticket
        serving_.v.fetch_add(1, std::memory_order_release);
    }

private:
    AlignedAtomic<std::uint32_t> next_{};    // occupy its own cache line
    AlignedAtomic<std::uint32_t> serving_{}; // and so does this one
};

// 预观测版 TicketLock：先观察是否空闲（serving == next），只有空闲时才 CAS 递增 next
// 注意：该版本不维护严格的 FIFO 排队，可能牺牲公平性以减少已锁状态下的原子写入
class TicketLockPreLoad : public iLock {
public:
    TicketLockPreLoad() = default;

    void lock() override {
        for (;;) {
            std::uint32_t s = serving_.v.load(std::memory_order_relaxed);
            std::uint32_t n = next_.v.load(std::memory_order_relaxed);
            if (s == n) {
                // 尝试在空闲时通过 CAS 领取票据
                std::uint32_t expected = n;
                if (next_.v.compare_exchange_weak(expected, n + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
                    // 我们的票据是 n，等待轮到我们（通常立即成功）
                    while (serving_.v.load(std::memory_order_acquire) != n) {
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
        serving_.v.fetch_add(1, std::memory_order_release);
    }

private:
    AlignedAtomic<std::uint32_t> next_{};
    AlignedAtomic<std::uint32_t> serving_{};
};

// Back-off ticket lock: spin delay proportional to distance (my - serving)
class TicketBackOff : public iLock {
public:
    TicketBackOff() = default;

    void lock() override {
        const std::uint32_t my = next_.v.fetch_add(1, std::memory_order_relaxed);
        for (;;) {
            std::uint32_t s = serving_.v.load(std::memory_order_acquire);
            if (s == my) break;
            // distance in queue (number of holders before us)
            std::uint32_t d = my - s; // wrap-around semantics are OK for 32-bit unsigned if queue is small
            // proportional relax: scale factor chosen empirically
            unsigned pauseIters = (d > 0 ? d - 1 : 0) * 32u + 16u;
            cpu_relax_n(pauseIters);
        }
    }

    void unlock() override {
        serving_.v.fetch_add(1, std::memory_order_release);
    }

private:
    AlignedAtomic<std::uint32_t> next_{};
    AlignedAtomic<std::uint32_t> serving_{};
};

// Back-off + write-prefetch: prefetch next_ for write before fetch_add, and back-off while waiting
class TicketBackOffAndPreFetch : public iLock {
public:
    TicketBackOffAndPreFetch() = default;

    void lock() override {
        prefetchw(&next_.v);
        const std::uint32_t my = next_.v.fetch_add(1, std::memory_order_relaxed);
        for (;;) {
            std::uint32_t s = serving_.v.load(std::memory_order_acquire);
            if (s == my) break;
            std::uint32_t d = my - s;
            unsigned pauseIters = (d > 0 ? d - 1 : 0) * 32u + 16u;
            cpu_relax_n(pauseIters);
        }
    }

    void unlock() override {
        prefetchw(&serving_.v);
        serving_.v.fetch_add(1, std::memory_order_release);
    }

private:
    AlignedAtomic<std::uint32_t> next_{};
    AlignedAtomic<std::uint32_t> serving_{};
};

// 简化自适应版：用“保守”的短 back-off，随队列距离 d 增加，但严格限制上限，避免错过 turn
// 目标：估算尽可能少而不是过多，保持低尾延迟，防止卡在某个线程
class TicketAdaptive : public iLock {
public:
    TicketAdaptive() = default;

    void lock() override {
        const std::uint32_t my = next_.v.fetch_add(1, std::memory_order_relaxed);
        for (;;) {
            std::uint32_t s = serving_.v.load(std::memory_order_acquire);
            if (s == my) break;
            std::uint32_t d = my - s; // 0 表示轮到我
            // 保守分段：近端快速探测，远端适度增加，但不超过上限
            unsigned pauseIters;
            if (d <= 1) {
                pauseIters = 16; // 几乎立刻重查，降低尾延迟
            } else if (d <= 4) {
                pauseIters = 32 + (d - 1) * 16; // 48,64,80
            } else if (d <= 16) {
                pauseIters = 128 + (d - 4) * 16; // 到 d=16 约 320
            } else {
                pauseIters = 512; // 上限：依机器可调，小而稳妥，避免过度等待
            }
            cpu_relax_n(pauseIters);
        }
    }

    void unlock() override {
        serving_.v.fetch_add(1, std::memory_order_release);
    }

private:
    AlignedAtomic<std::uint32_t> next_{};
    AlignedAtomic<std::uint32_t> serving_{};
};

} // namespace lt
