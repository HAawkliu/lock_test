#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <chrono>

#include "iLock.h"
#include "iRunTask.h"

namespace lt {

class LockTestSys {
public:
    LockTestSys(std::unique_ptr<iLock> lock,
                std::unique_ptr<iRunTask> task,
                int numThreads,
                double durationSeconds)
        : lock_(std::move(lock)), task_(std::move(task)), numThreads_(numThreads), durationSeconds_(durationSeconds) {}

    // Run with lock for a fixed duration; threads compete for lock and call task->run().
    // Returns total operations completed across all threads.
    std::uint64_t run_test();

    // No atomic-only path in this mode.

    int threads() const { return numThreads_; }
    double durationSeconds() const { return durationSeconds_; }

private:
    std::unique_ptr<iLock> lock_;
    std::unique_ptr<iRunTask> task_;
    int numThreads_ {4};
    double durationSeconds_ {1.0};
};

} // namespace lt
