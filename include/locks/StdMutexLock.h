#pragma once

#include "iLock.h"
#include <mutex>

namespace lt {

// A lock implementation using std::mutex
class StdMutexLock : public iLock {
public:
    void lock() override { m_.lock(); }
    void unlock() override { m_.unlock(); }

private:
    std::mutex m_;
};

} // namespace lt
