#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>

#include "locks/StdMutexLock.h"
#include "lockTestSys.h"

using namespace lt;

struct Args {
    int threads = 4;
    std::string runTask = "sum";
    std::string lockKind = "mutex";
    int repeats = 5; // internal multiple runs for averaging
    std::uint64_t iterations = 20'000'000; // default large number
};

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [-t threads] [-r task] [-l lock]" << "\n";
    std::cout << "  -t threads   number of pthreads (default 4)\n";
    std::cout << "  -r task      runtask name: sum (default)\n";
    std::cout << "  -l lock      lock kind: mutex (default)\n";
}

static bool parse_args(int argc, char** argv, Args& out) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-t" && i + 1 < argc) {
            out.threads = std::atoi(argv[++i]);
        } else if (a == "-r" && i + 1 < argc) {
            out.runTask = argv[++i];
        } else if (a == "-l" && i + 1 < argc) {
            out.lockKind = argv[++i];
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown or incomplete option: " << a << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    if (out.threads <= 0) out.threads = 1;
    return true;
}

static std::unique_ptr<iLock> make_lock(const std::string& name) {
    if (name == "mutex") {
        return std::make_unique<StdMutexLock>();
    }
    return nullptr;
}

static std::unique_ptr<iRunTask> make_task(const std::string& name, std::uint64_t iterations) {
    if (name == "sum") {
        return std::make_unique<SumTask>(iterations);
    }
    return nullptr;
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    auto lock = make_lock(args.lockKind);
    if (!lock) {
        std::cerr << "Unknown lock kind: " << args.lockKind << "\n";
        return 2;
    }
    auto task = make_task(args.runTask, args.iterations);
    if (!task) {
        std::cerr << "Unknown runtask: " << args.runTask << "\n";
        return 3;
    }

    LockTestSys sys(std::move(lock), std::move(task), args.threads, args.iterations);

    std::vector<std::uint64_t> lock_times;
    std::vector<std::uint64_t> atomic_times;
    lock_times.reserve(args.repeats);
    atomic_times.reserve(args.repeats);

    for (int i = 0; i < args.repeats; ++i) {
        auto t_lock = sys.run_test();
        auto t_atomic = sys.run_atomic();
        lock_times.push_back(t_lock);
        atomic_times.push_back(t_atomic);
    }

    auto avg = [](const std::vector<std::uint64_t>& v) {
        std::uint64_t s = 0; for (auto x : v) s += x; return v.empty() ? 0 : s / v.size();
    };

    auto avg_lock = avg(lock_times);
    auto avg_atomic = avg(atomic_times);

    std::cout << "Threads: " << args.threads
              << ", Task: " << args.runTask
              << ", Lock: " << args.lockKind
              << ", Iterations: " << args.iterations
              << ", Repeats: " << args.repeats << "\n";

    std::cout << "Average time (lock):   " << avg_lock   << " us\n";
    std::cout << "Average time (atomic): " << avg_atomic << " us\n";
    
    if (avg_atomic > 0) {
        double ratio = static_cast<double>(avg_lock) / static_cast<double>(avg_atomic);
        std::cout << "Lock/Atomic ratio: " << ratio << "x\n";
    }

    std::cout << "Average visits per microsecond: " << 
        static_cast<double>(args.iterations) / (static_cast<double>(avg_lock)) << " (lock), "
              << static_cast<double>(args.iterations) / (static_cast<double>(avg_atomic)) << " (atomic)\n";
    return 0;
}
