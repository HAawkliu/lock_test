#pragma once

#include <memory>

namespace lt {

// Base lock interface
class iLock {
public:
    virtual ~iLock() = default;
    virtual void lock() = 0;
    virtual void unlock() = 0;
};

// Simple RAII guard for iLock
class LockGuard {
public:
    explicit LockGuard(iLock& lock) : lock_(lock) { lock_.lock(); }
    ~LockGuard() { lock_.unlock(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    iLock& lock_;
};

} // namespace lt
