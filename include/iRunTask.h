#pragma once

#include <cstdint>
#include <atomic>

namespace lt {

// Base run task interface, cooperates with locking framework.
// Contract:
// - run() executes one minimal unit of work; it is called while holding the lock in lock-based tests.
// - run_atomic() executes one minimal unit of work using atomics only; must be safe under concurrent calls.
// - Both return true if the task is finished after this call; false otherwise. When already finished, they should be idempotent.
class iRunTask {
public:
    virtual ~iRunTask() = default;
    virtual void reset() = 0;
    virtual bool run() = 0;           // protected by external lock
    virtual bool run_atomic() = 0;     // concurrent, lock-free path
    virtual const char* name() const = 0;
};

// A concrete task: increment a counter until reaching a target.
class SumTask : public iRunTask {
public:
    explicit SumTask(std::uint64_t target) : target_(target) { reset(); }

    void reset() override {
        current_ = 0;                           // for lock-based path
        current_atomic_.store(0, std::memory_order_relaxed); // for atomic path
    }

    // Run one increment under external lock; do nothing if already finished.
    bool run() override {
        if (current_ >= target_) return true; // already done
        ++current_;
        return current_ >= target_;
    }

    // Lock-free single step using atomic increment; allows slight overshoot like standard fetch_add loop.
    bool run_atomic() override {
        auto prev = current_atomic_.fetch_add(1, std::memory_order_relaxed);
        return prev + 1 >= target_;
    }

    const char* name() const override { return "sum"; }

    std::uint64_t current() const { return current_; }
    std::uint64_t target() const { return target_; }

private:
    // State for lock-protected path
    std::uint64_t current_ {0};
    // State for atomic path
    std::atomic<std::uint64_t> current_atomic_ {0};
    // Shared target
    const std::uint64_t target_ {0};
};

} // namespace lt
