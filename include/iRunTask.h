#pragma once

#include <cstdint>

namespace lt {

// Base run task interface, cooperates with locking framework.
// Contract:
// - run() executes one minimal unit of work in the lock-based path.
// - run_atomic() executes one minimal unit of work using atomics only; must be safe under concurrent calls.

class iRunTask {
public:
    virtual ~iRunTask() = default;
    virtual void reset() = 0;
    virtual bool run() = 0;           // protected by external lock
    virtual const char* name() const = 0;
};

// A concrete task: do nothing under lock; in atomic path, perform a relaxed atomic increment
// to simulate the minimal atomic operation cost.
class DoNothingTask : public iRunTask {
public:
    DoNothingTask() { reset(); }

    void reset() override {
        // nothing to reset for do_nothing
    }

    // Lock-based path: do nothing to isolate lock overhead
    bool run() override {
        // intentionally empty
        return false; 
    }

    const char* name() const override { return "do_nothing"; }

private:
    // no state
};

// A CPU-bound task that performs a small amount of arithmetic work per run
// without touching shared memory to minimize cache contention.
class CpuBurnTask : public iRunTask {
public:
    explicit CpuBurnTask(int workIters = 256) : workIters_(workIters) {}

    void reset() override {
        // stateless
    }

    bool run() override {
        volatile std::uint64_t x = 0x9e3779b97f4a7c15ULL; // per-call local to avoid sharing
        // Do a small xorshift-like scramble loop
        for (int i = 0; i < workIters_; ++i) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
        }
        (void)x; // prevent unused warnings
        return false; // duration-controlled loop ignores return
    }

    const char* name() const override { return "cpu_burn"; }

private:
    int workIters_;
};

} // namespace lt
