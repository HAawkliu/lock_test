#pragma once

#include <cstdint>

namespace lt {

// Base run task interface, cooperates with locking framework.
// Contract:
// - run_parallel(): the major portion of work that can proceed without holding the lock.
// - run_locked(): the small critical section that must be protected by the external lock.

class iRunTask {
public:
    virtual ~iRunTask() = default;
    virtual void reset() = 0;
    virtual void run_parallel() = 0;   // executed outside of lock
    virtual void run_locked() = 0;     // executed under external lock
    virtual const char* name() const = 0;
};

// A concrete task: do nothing in both phases to isolate lock overhead.
class DoNothingTask : public iRunTask {
public:
    DoNothingTask() { reset(); }

    void reset() override {
        // nothing to reset for do_nothing
    }

    // No parallel work
    void run_parallel() override { /* intentionally empty */ }

    // Lock-based path: still do nothing to isolate lock overhead
    void run_locked() override { /* intentionally empty */ }

    const char* name() const override { return "do_nothing"; }

private:
    // no state
};

// A CPU-bound task that performs a small amount of arithmetic work per run
// without touching shared memory to minimize cache contention.
class CpuBurnTask : public iRunTask {
public:
    // split the work into parallel and locked portions; keep locked part relatively small by default
    explicit CpuBurnTask(int parallelIters = 2048, int lockedIters = 32)
        : parallelIters_(parallelIters), lockedIters_(lockedIters) {}

    void reset() override {
        // stateless
    }

    void run_parallel() override { do_scramble(parallelIters_); }

    void run_locked() override { do_scramble(lockedIters_); }

    const char* name() const override { return "cpu_burn"; }

private:
    static inline void do_scramble(int iters) {
        volatile std::uint64_t x = 0x9e3779b97f4a7c15ULL; // per-call local to avoid sharing
        // Do a small xorshift-like scramble loop
        for (int i = 0; i < iters; ++i) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
        }
        (void)x; // prevent unused warnings
    }

    int parallelIters_;
    int lockedIters_;
};

} // namespace lt
