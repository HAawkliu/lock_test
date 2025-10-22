#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <iomanip>

#include "locks/StdMutexLock.h"
#include "locks/TasSpinlock.h"
#include "locks/TicketLock.h"
#include "locks/McsLock.h"
#include "lockTestSys.h"

using namespace lt;

struct Args {
    int threads = 0; // 0 表示未显式指定，触发批量线程数测试
    std::string runTask = "do_nothing";
    std::string lockKind = "mutex";
    int repeats = 5; // internal multiple runs for averaging
    double duration = 2.0; // seconds per test run
    bool explicitThreads = false; // 是否由 -t 显式指定
};

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [-t threads] [-r task] [-l lock] [-d seconds]" << "\n";
    std::cout << "  -t threads   number of pthreads (default runs 1,2,4,8,16,32 when omitted)\n";
    std::cout << "  -r task      runtask name: do_nothing (default)\n";
    std::cout << "  -l lock      lock kind: mutex (default)\n";
    std::cout << "  -d seconds   duration per run in seconds (default 2.0)\n";
}

static bool parse_args(int argc, char** argv, Args& out) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-t" && i + 1 < argc) {
            out.threads = std::atoi(argv[++i]);
            out.explicitThreads = true;
        } else if (a == "-r" && i + 1 < argc) {
            out.runTask = argv[++i];
        } else if (a == "-l" && i + 1 < argc) {
            out.lockKind = argv[++i];
        } else if (a == "-d" && i + 1 < argc) {
            out.duration = std::atof(argv[++i]);
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown or incomplete option: " << a << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    if (out.explicitThreads && out.threads <= 0) out.threads = 1;
    if (out.duration <= 0.0) out.duration = 1.0;
    return true;
}

static std::unique_ptr<iLock> make_lock(const std::string& name) {
    if (name == "mutex") {
        return std::make_unique<StdMutexLock>();
    }
    if (name == "tas" || name == "spin" || name == "tas_spin") {
        return std::make_unique<TasSpinlock>();
    }
    if (name == "ticket") {
        return std::make_unique<TicketLock>();
    }
    if (name == "mcs") {
        return std::make_unique<McsLock>();
    }
    return nullptr;
}

static std::unique_ptr<iRunTask> make_task(const std::string& name, std::uint64_t /*unused*/) {
    if (name == "do_nothing") {
        return std::make_unique<DoNothingTask>();
    }
    if (name == "cpu_burn" || name == "compute") {
        return std::make_unique<CpuBurnTask>();
    }
    return nullptr;
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    // 构造要测试的线程数列表
    std::vector<int> threadCounts;
    if (args.explicitThreads) {
        threadCounts.push_back(args.threads);
    } else {
        threadCounts = {1, 2, 4, 8, 16, 32};
    }

    std::cout.setf(std::ios::fixed); std::cout.precision(2);
    std::cout << "Task: " << args.runTask
              << ", Lock: " << args.lockKind
              << ", Duration: " << args.duration << " s"
              << ", Repeats: " << args.repeats << "\n";

    // Table header
    std::cout << "\n"
              << std::left << std::setw(10) << "Threads"
              << std::right << std::setw(20) << "Avg Ops"
              << std::setw(20) << "Ops/s" << "\n";
    std::cout << std::string(50, '-') << "\n";

    auto avg = [](const std::vector<std::uint64_t>& v) {
        long double s = 0; for (auto x : v) s += x; return v.empty() ? 0.0 : static_cast<double>(s / v.size());
    };

    for (int tc : threadCounts) {
        auto lock = make_lock(args.lockKind);
        if (!lock) {
            std::cerr << "Unknown lock kind: " << args.lockKind << "\n";
            return 2;
        }
        auto task = make_task(args.runTask, 0);
        if (!task) {
            std::cerr << "Unknown runtask: " << args.runTask << "\n";
            return 3;
        }

        LockTestSys sys(std::move(lock), std::move(task), tc, args.duration);

        std::vector<std::uint64_t> lock_ops;
        lock_ops.reserve(args.repeats);
        for (int i = 0; i < args.repeats; ++i) {
            auto ops_lock = sys.run_test();
            lock_ops.push_back(ops_lock);
        }

    double avg_lock_ops = avg(lock_ops);
    double lock_qps = avg_lock_ops / args.duration;

    // Table row
    std::cout << std::left << std::setw(10) << tc
          << std::right << std::setw(20) << avg_lock_ops
          << std::setw(20) << lock_qps << "\n";
    }
    return 0;
}
