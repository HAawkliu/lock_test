#pragma once

#include "iLock.h"
#include <atomic>
#include <cstdint>
#include <unordered_map>

namespace lt {
// Determine cache line size at compile time; fallback to 64 if not available
#if defined(__cpp_lib_hardware_interference_size)
constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
constexpr std::size_t kCacheLineSize = 64;
#endif
static_assert(kCacheLineSize >= sizeof(std::uint64_t), "Cache line size must be >= 8 bytes");
// MCS queue lock: scalable FIFO spinlock

class McsLock : public iLock {
public:
    McsLock() = default;

    void lock() override {
        Node& me = node_for_this_thread();
        me.next.store(nullptr, std::memory_order_relaxed);
        me.locked.store(true, std::memory_order_relaxed);

        Node* prev = tail_.exchange(&me, std::memory_order_acq_rel);
        if (prev != nullptr) {
            // Link ourselves after predecessor
            prev->next.store(&me, std::memory_order_release);
            // Wait until predecessor clears our locked flag
            while (me.locked.load(std::memory_order_acquire)) {
                // busy-wait
            }
        } else {
            // We acquired the lock directly
            me.locked.store(false, std::memory_order_relaxed);
        }
    }

    void unlock() override {
        Node& me = node_for_this_thread();
        Node* succ = me.next.load(std::memory_order_acquire);
        if (succ == nullptr) {
            // Try to release the lock if no known successor
            Node* expected = &me;
            if (tail_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                // No successor; lock fully released
                return;
            }
            // Wait for successor to appear
            do {
                succ = me.next.load(std::memory_order_acquire);
            } while (succ == nullptr);
        }
        // Pass the lock to successor
        succ->locked.store(false, std::memory_order_release);
        // Help GC: break the link
        me.next.store(nullptr, std::memory_order_relaxed);
    }

private:


    struct alignas(kCacheLineSize) Node {
        std::atomic<Node*> next{nullptr};
        std::atomic<bool> locked{false};
        // pad to occupy (at least) one cache line, reducing false sharing
        char pad[kCacheLineSize - (sizeof(std::atomic<Node*>) + sizeof(std::atomic<bool>)) > 0
                 ? kCacheLineSize - (sizeof(std::atomic<Node*>) + sizeof(std::atomic<bool>))
                 : 1]{};
    };
    static_assert(alignof(Node) >= kCacheLineSize, "MCS Node should be 64-byte aligned");

    // One node per (lock, thread). Implemented via a thread_local map keyed by this.
    Node& node_for_this_thread() {
        static thread_local std::unordered_map<const McsLock*, Node> map;
        return map[this]; // default-construct if not present
    }

    std::atomic<Node*> tail_{nullptr};
};

// 预观测版 MCS：先观察 tail 是否为空，仅在空闲时 CAS 占位；不排队，牺牲公平以避免在拥塞时写入
class McsLockPreLoad : public iLock {
public:
    McsLockPreLoad() = default;

    void lock() override {
        Node& me = node_for_this_thread();
        me.next.store(nullptr, std::memory_order_relaxed);
        me.locked.store(true, std::memory_order_relaxed);

        for (;;) {
            Node* t = tail_.load(std::memory_order_relaxed);
            if (t != nullptr) {
                // 已有持有者/等待者，避免写入，继续观察
                continue;
            }
            Node* expected = nullptr;
            if (tail_.compare_exchange_weak(expected, &me,
                                            std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
                // 直接获得锁（无前驱，无需排队）
                me.locked.store(false, std::memory_order_relaxed);
                return;
            }
        }
    }

    void unlock() override {
        Node& me = node_for_this_thread();
        // 预期无后继者，直接将 tail 从自己清空
        Node* expected = &me;
        (void)tail_.compare_exchange_strong(expected, nullptr,
                                            std::memory_order_acq_rel,
                                            std::memory_order_relaxed);
        me.next.store(nullptr, std::memory_order_relaxed);
    }

private:
    struct alignas(kCacheLineSize) Node {
        std::atomic<Node*> next{nullptr};
        std::atomic<bool> locked{false};
        char pad[kCacheLineSize - (sizeof(std::atomic<Node*>) + sizeof(std::atomic<bool>)) > 0
                 ? kCacheLineSize - (sizeof(std::atomic<Node*>) + sizeof(std::atomic<bool>))
                 : 1]{};
    };
    static_assert(alignof(Node) >= kCacheLineSize, "MCS Node should be 64-byte aligned");

    Node& node_for_this_thread() {
        static thread_local std::unordered_map<const McsLockPreLoad*, Node> map;
        return map[this];
    }

    std::atomic<Node*> tail_{nullptr};
};

} // namespace lt
