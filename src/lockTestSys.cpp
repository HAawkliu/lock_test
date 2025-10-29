#include "lockTestSys.h"

#include <pthread.h>
#include <cassert>
#include <cstdint>
#include <vector>
#include <new>
#include <chrono>
#include <atomic>
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

struct SharedTiming {
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> ready{0};
    double endTime{0.0}; // legacy, not used with per-thread timing
    double durationSeconds{0.0};
    int total{0};
};

struct ThreadCtxLock {
    iLock* lock;
    iRunTask* task;
    SharedTiming* timing;
    ThreadResult* resultSlot;
};

void* thread_func_lock(void* arg) {
    auto* ctx = static_cast<ThreadCtxLock*>(arg);
    std::uint64_t localCount = 0;
    // signal ready and wait for synchronized start
    ctx->timing->ready.fetch_add(1, std::memory_order_acq_rel);
    while (!ctx->timing->start.load(std::memory_order_acquire)) {
        // spin until main thread starts the test window
    }
    // Per-thread local end time to avoid cross-core TSC skew issues
    const double localEnd = CycleTimer::currentSeconds() + ctx->timing->durationSeconds;
    const int checkEvery = 64; // amortize time checks, keep overshoot bounded
    for (;;) {
        if ((localCount & (checkEvery - 1)) == 0) {
            if (ctx->timing->stop.load(std::memory_order_acquire) ||
                CycleTimer::currentSeconds() >= localEnd) {
                break;
            }
        }
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
    SharedTiming timing;
    timing.total = numThreads_;

    std::vector<ThreadCtxLock*> ctx_ptrs(numThreads_, nullptr);
    for (int i = 0; i < numThreads_; ++i) {
        ctx_ptrs[i] = new ThreadCtxLock{ lock_.get(), task_.get(), &timing, &results[i] };
        pthread_create(&threads[i], nullptr, &thread_func_lock, ctx_ptrs[i]);
    }
    // wait for all threads to be ready
    while (timing.ready.load(std::memory_order_acquire) < numThreads_) {
        // spin
    }
    // broadcast duration and start flag (threads compute local end time)
    timing.durationSeconds = durationSeconds_;
    timing.start.store(true, std::memory_order_release);

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
