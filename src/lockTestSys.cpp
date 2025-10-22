#include "lockTestSys.h"

#include <pthread.h>
#include <cassert>
#include <cstdint>
#include <vector>
#include <new>
#include <chrono>
#include "utils/CycleTimer.h"

namespace lt {

namespace {
// Determine cache line size at compile time; fallback to 64 if not available
#if defined(__cpp_lib_hardware_interference_size)
constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
constexpr std::size_t kCacheLineSize = 64;
#endif
static_assert(kCacheLineSize >= sizeof(std::uint64_t), "Cache line size must be >= 8 bytes");

// Padding to reduce false sharing when storing results
struct alignas(kCacheLineSize) ThreadResult {
    std::uint64_t count;
    char pad[kCacheLineSize - sizeof(std::uint64_t)];
};
static_assert(sizeof(ThreadResult) == kCacheLineSize, "ThreadResult should occupy exactly one cache line");

struct ThreadCtxLock {
    iLock* lock;
    iRunTask* task;
    double endTime;
    ThreadResult* resultSlot;
};

void* thread_func_lock(void* arg) {
    auto* ctx = static_cast<ThreadCtxLock*>(arg);
    std::uint64_t localCount = 0;
    while (CycleTimer::currentSeconds() < ctx->endTime) {
        // majority of work that can run without lock
        ctx->task->run_parallel();

        // critical section protected by the lock
        ctx->lock->lock();
        ctx->task->run_locked();
        ctx->lock->unlock();
        ++localCount;
    }
    ctx->resultSlot->count = localCount; // single write on exit
    return nullptr;
}

} // namespace

std::uint64_t LockTestSys::run_test() {
    assert(lock_ && task_);
    task_->reset();

    std::vector<pthread_t> threads(numThreads_);
    std::vector<ThreadResult> results(numThreads_);
    double endTime = CycleTimer::currentSeconds() + durationSeconds_;

    std::vector<ThreadCtxLock*> ctx_ptrs(numThreads_, nullptr);
    for (int i = 0; i < numThreads_; ++i) {
        ctx_ptrs[i] = new ThreadCtxLock{ lock_.get(), task_.get(), endTime, &results[i] };
        pthread_create(&threads[i], nullptr, &thread_func_lock, ctx_ptrs[i]);
    }
    std::uint64_t total = 0;
    for (int i = 0; i < numThreads_; ++i) {
        pthread_join(threads[i], nullptr);
    }
    for (int i = 0; i < numThreads_; ++i) {
        delete ctx_ptrs[i];
    }
    for (int i = 0; i < numThreads_; ++i) {
        total += results[i].count;
    }
    return total;
}
 

} // namespace lt
