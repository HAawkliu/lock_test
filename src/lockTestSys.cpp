#include "lockTestSys.h"

#include <pthread.h>
#include <atomic>
#include <cassert>
#include "utils/CycleTimer.h"

namespace lt {

namespace {
struct ThreadCtxLock {
    iLock* lock;
    iRunTask* task;
};

void* thread_func_lock(void* arg) {
    auto* ctx = static_cast<ThreadCtxLock*>(arg);
    // Loop until task signals finished; guard ensures mutual exclusion
    while (true) {
        ctx->lock->lock();
        bool finished = ctx->task->run();
        ctx->lock->unlock();
        if (finished) break;
    }
    return nullptr;
}

struct ThreadCtxAtomicTask {
    iRunTask* task;
};

void* thread_func_atomic(void* arg) {
    auto* ctx = static_cast<ThreadCtxAtomicTask*>(arg);
    while (true) {
        if (ctx->task->run_atomic()) break;
    }
    return nullptr;
}
} // namespace

std::uint64_t LockTestSys::run_test() {
    assert(lock_ && task_);
    task_->reset();

    std::vector<pthread_t> threads(numThreads_);
    ThreadCtxLock ctx{ lock_.get(), task_.get() };

    double t0 = CycleTimer::currentSeconds();
    for (int i = 0; i < numThreads_; ++i) {
        pthread_create(&threads[i], nullptr, &thread_func_lock, &ctx);
    }
    for (auto& th : threads) {
        pthread_join(th, nullptr);
    }
    double t1 = CycleTimer::currentSeconds();
    double secs = t1 - t0;
    return static_cast<std::uint64_t>(secs * 1e6);
}

std::uint64_t LockTestSys::run_atomic() {
    assert(task_);
    task_->reset();
    std::vector<pthread_t> threads(numThreads_);
    ThreadCtxAtomicTask ctx{ task_.get() };

    double t0 = CycleTimer::currentSeconds();
    for (int i = 0; i < numThreads_; ++i) {
        pthread_create(&threads[i], nullptr, &thread_func_atomic, &ctx);
    }
    for (auto& th : threads) {
        pthread_join(th, nullptr);
    }
    double t1 = CycleTimer::currentSeconds();
    double secs = t1 - t0;
    return static_cast<std::uint64_t>(secs * 1e6);
}

} // namespace lt
