#pragma once

#include "iLock.h"
#include <atomic>
#include <cstdint>
#include <thread>
#if defined(__linux__)
    #include <sched.h>
#endif

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

// Tuning constants inspired by reference implementation
constexpr unsigned TICKET_BASE_WAIT = 4u;
constexpr unsigned TICKET_MAX_WAIT  = 32u; // reserved for potential ramping
constexpr unsigned TICKET_WAIT_NEXT = 1u;

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



// Back-off ticket lock: spin delay proportional to distance (my - serving)
class TicketBackOff : public iLock {
public:
    TicketBackOff() {
        // Match libslock init: head=1, tail=0
        next_.v.store(0, std::memory_order_relaxed);
        serving_.v.store(1, std::memory_order_relaxed);
    }

    void lock() override {
        // libslock uses IAF_U32 (increment-and-fetch). Emulate with fetch_add + 1
        const std::uint32_t my = next_.v.fetch_add(1, std::memory_order_relaxed) + 1;
        unsigned wait = TICKET_BASE_WAIT;           // libslock: TICKET_BASE_WAIT
        std::uint32_t distance_prev = 1;            // reset wait if queue length changes
        for (;;) {
            const std::uint32_t s = serving_.v.load(std::memory_order_acquire);
            if (s == my) break;
            const std::uint32_t distance = (my >= s) ? (my - s) : (s - my); // match libslock sub_abs

            if (distance > 1) {
                if (distance != distance_prev) {
                    distance_prev = distance;
                    wait = TICKET_BASE_WAIT;
                }
                // Optional ramping (disabled to avoid oversleep):
                // wait = (wait + TICKET_BASE_WAIT) & TICKET_MAX_WAIT;
                cpu_relax_n(distance * wait);
            } else {
                cpu_relax_n(TICKET_WAIT_NEXT);
            }

            if (distance > 20) {
#if defined(__linux__)
                sched_yield();
#elif defined(_WIN32)
                SwitchToThread();
#else
                std::this_thread::yield();
#endif
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

// Back-off + write-prefetch: prefetch next_ for write before fetch_add, and back-off while waiting
class TicketBackOffAndPreFetch : public iLock {
public:
    TicketBackOffAndPreFetch() {
        // Match libslock init: head=1, tail=0
        next_.v.store(0, std::memory_order_relaxed);
        serving_.v.store(1, std::memory_order_relaxed);
    }

    void lock() override {
        // Prefetch disabled for portability; write-prefetch may cause stalls on some CPUs.
        prefetchw(&next_.v);
        const std::uint32_t my = next_.v.fetch_add(1, std::memory_order_relaxed) + 1;
        unsigned wait = TICKET_BASE_WAIT;
        std::uint32_t distance_prev = 1;
        for (;;) {
            const std::uint32_t s = serving_.v.load(std::memory_order_acquire);
            if (s == my) break;
            const std::uint32_t distance = (my >= s) ? (my - s) : (s - my);

            if (distance > 1) {
                if (distance != distance_prev) {
                    distance_prev = distance;
                    wait = TICKET_BASE_WAIT;
                }
                // Optional ramping (disabled):
                // wait = (wait + TICKET_BASE_WAIT) & TICKET_MAX_WAIT;
                cpu_relax_n(distance * wait);
            } else {
                cpu_relax_n(TICKET_WAIT_NEXT);
            }

            if (distance > 20) {
#if defined(__linux__)
                sched_yield();
#elif defined(_WIN32)
                SwitchToThread();
#else
                std::this_thread::yield();
#endif
            }
        }
    }

    void unlock() override {
    // Prefetch disabled for portability.
        prefetchw(&serving_.v);
        serving_.v.fetch_add(1, std::memory_order_release);
    }

private:
    AlignedAtomic<std::uint32_t> next_{};
    AlignedAtomic<std::uint32_t> serving_{};
};



} // namespace lt
