#include "lockTestSys.h"

#include <pthread.h>
#include <cassert>
#include <cstdint>
#include <vector>
#include <new>
#include <chrono>
#include <atomic>
#include <thread>
#include <time.h>
#if defined(__linux__)
#include <unistd.h>
#include <sched.h>
#endif
// Adopt libslock-style timing: coordinated start, main-thread sleep window, global stop flag

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
    double endTime{0.0}; // unused with libslock-style timing
    double durationSeconds{0.0}; // broadcasted window length (seconds)
    int total{0};
};

struct ThreadCtxLock {
    iLock* lock;
    iRunTask* task;
    SharedTiming* timing;
    ThreadResult* resultSlot;
    int cpuId; // target CPU id for pinning
};

void* thread_func_lock(void* arg) {
    auto* ctx = static_cast<ThreadCtxLock*>(arg);
    // Bind this thread to a specific CPU if available (Linux)
#if defined(__linux__)
    if (ctx->cpuId >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(static_cast<unsigned>(ctx->cpuId), &cpuset);
        (void)pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
#endif
    std::uint64_t localCount = 0;
    // signal ready and wait for synchronized start
    ctx->timing->ready.fetch_add(1, std::memory_order_acq_rel);
    while (!ctx->timing->start.load(std::memory_order_acquire)) {
        // spin until main thread starts the test window
    }
    // Per-thread local end time to avoid cross-core TSC skew issues
    const int checkEvery = 64; // amortize time checks, keep overshoot bounded
    for (;;) {
        if ((localCount & (checkEvery - 1)) == 0) {
            if (ctx->timing->stop.load(std::memory_order_acquire)) {
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

    // Determine CPU count and build a simple round-robin mapping
    int ncpu = 1;
#if defined(__linux__)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) ncpu = static_cast<int>(n);
#else
    unsigned hc = std::thread::hardware_concurrency();
    if (hc > 0) ncpu = static_cast<int>(hc);
#endif

    std::vector<ThreadCtxLock*> ctx_ptrs(numThreads_, nullptr);
    for (int i = 0; i < numThreads_; ++i) {
        int cpuId = (ncpu > 0) ? (i % ncpu) : -1;
        ctx_ptrs[i] = new ThreadCtxLock{ lock_.get(), task_.get(), &timing, &results[i], cpuId };
        pthread_create(&threads[i], nullptr, &thread_func_lock, ctx_ptrs[i]);
    }
    // wait for all threads to be ready
    while (timing.ready.load(std::memory_order_acquire) < numThreads_) {
        // spin
    }
    // broadcast duration and start flag (threads compute local end time)
    timing.durationSeconds = durationSeconds_;
    timing.start.store(true, std::memory_order_release);
    // Main thread controls test window like libslock (nanosleep + stop flag)
    {
        timespec ts;
        ts.tv_sec = static_cast<time_t>(durationSeconds_);
        ts.tv_nsec = static_cast<long>((durationSeconds_ - static_cast<double>(ts.tv_sec)) * 1e9);
        // Ensure tv_nsec in range
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += ts.tv_nsec / 1000000000L;
            ts.tv_nsec = ts.tv_nsec % 1000000000L;
        }
        // Sleep for the requested duration (ignore EINTR for simplicity)
        nanosleep(&ts, nullptr);
    }
    timing.stop.store(true, std::memory_order_release);

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
