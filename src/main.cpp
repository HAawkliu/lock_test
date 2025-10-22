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
    // cpu_burn 配置（多数并行，少数加锁）
    int cpuParallelIters = -1; // <0 表示采用默认值
    int cpuLockedIters = -1;   // <0 表示采用默认值
};

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [-t threads] [-r task] [-l lock] [-d seconds]" << "\n";
    std::cout << "  -t threads   number of pthreads (default runs 1,2,4,8,16,32 when omitted)\n";
    std::cout << "  -r task      runtask name: do_nothing (default)\n";
    std::cout << "  -l lock      lock kind: mutex (default)\n";
    std::cout << "  -d seconds   duration per run in seconds (default 2.0)\n";
    std::cout << "  -R p[:l]     cpu_burn iters: parallel p, locked l (default p=2048,l=32)\n";
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
        } else if (a == "-R" && i + 1 < argc) {
            std::string v = argv[++i];
            // 支持 "p:l" 或 "p,l" 或仅 "p"
            auto parse_pair = [](const std::string& s, int& p, int& l) {
                size_t pos = s.find(':');
                if (pos == std::string::npos) pos = s.find(',');
                try {
                    if (pos == std::string::npos) {
                        p = std::stoi(s);
                    } else {
                        p = std::stoi(s.substr(0, pos));
                        l = std::stoi(s.substr(pos + 1));
                    }
                } catch (...) {
                    p = -1; l = -1;
                }
            };
            int p = -1, l = -1;
            parse_pair(v, p, l);
            if (p > 0) out.cpuParallelIters = p;
            if (l > 0) out.cpuLockedIters = l;
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

static std::unique_ptr<iRunTask> make_task(const std::string& name, std::uint64_t /*unused*/, int cpuParallelIters, int cpuLockedIters) {
    if (name == "do_nothing") {
        return std::make_unique<DoNothingTask>();
    }
    if (name == "cpu_burn" || name == "compute") {
        // 应用 CLI 覆盖，缺省采用 CpuBurnTask 默认值
        int p = (cpuParallelIters > 0) ? cpuParallelIters : 2048;
        int l = (cpuLockedIters > 0) ? cpuLockedIters : 32;
        return std::make_unique<CpuBurnTask>(p, l);
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
        threadCounts = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 32};
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
        auto task = make_task(args.runTask, 0, args.cpuParallelIters, args.cpuLockedIters);
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
