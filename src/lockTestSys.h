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
                std::uint64_t iterations)
        : lock_(std::move(lock)), task_(std::move(task)), numThreads_(numThreads), iterations_(iterations) {}

    // Run with lock: threads compete for lock and call task->run() until finished.
    // Returns elapsed microseconds.
    std::uint64_t run_test();

    // Run with atomics only: threads increment a shared atomic counter until target.
    // Returns elapsed microseconds.
    std::uint64_t run_atomic();

    int threads() const { return numThreads_; }
    std::uint64_t iterations() const { return iterations_; }

private:
    std::unique_ptr<iLock> lock_;
    std::unique_ptr<iRunTask> task_;
    int numThreads_ {4};
    std::uint64_t iterations_ {10'000'000};
};

} // namespace lt
