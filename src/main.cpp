#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>

#include "locks/StdMutexLock.h"
#include "locks/TasSpinlock.h"
#include "locks/TicketLock.h"
#include "locks/McsLock.h"
#include "lockTestSys.h"

using namespace lt;

struct Args {
    // 任务类型、锁列表、分段线程集、重复次数、时长、cpu_burn 比例、CSV 文件输出、是否仅 CSV
    std::string runTask = "cpu_burn";   // 支持 cpu_burn | do_nothing（可扩展）
    std::vector<std::string> locks;     // -L mutex,spin,ticket,mcs
    std::string threadBins;             // -B 1-64:1,65-128:8
    int repeats = 5;                    // -n 重复次数
    double duration = 2.0;              // -d 每组时长（秒）
    int cpuParallelIters = 2048;        // -R p[:l] 并行迭代
    int cpuLockedIters = 32;            // -R p[:l] 加锁迭代
    std::string csvFile;                // --csv-file 输出 CSV 文件
    bool csvOnly = false;               // --csv-only 仅 CSV
};

static void print_usage(const char* prog) {
    std::cout << "Usage:\n"
              << "  " << prog << " -r <task> -L mutex,spin,ticket,mcs \\\n" 
              << "    -B 1-64:1,65-128:8 -n 5 -d 1.0 -R 2048:32 \\\n" 
              << "    --csv-file results.csv [--csv-only]\n";
    std::cout << "  -r task       task kind: cpu_burn | do_nothing\n";
    std::cout << "  -L locks      comma-separated locks: mutex,spin,ticket,mcs\n";
    std::cout << "  -B bins       thread bins: e.g. 1-64:1,65-128:8 (inclusive; step default=1)\n";
    std::cout << "  -n repeats    repeats per setting (default 5)\n";
    std::cout << "  -d seconds    duration per run in seconds (default 2.0)\n";
    std::cout << "  -R p[:l]      cpu_burn iters: parallel p, locked l (default 2048:32)\n";
    std::cout << "  --csv-file f  write CSV to file path f (with header)\n";
    std::cout << "  --csv-only    suppress formatted table (CSV only)\n";
}

static bool parse_args(int argc, char** argv, Args& out) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-B" && i + 1 < argc) {
            out.threadBins = argv[++i];
        } else if (a == "-L" && i + 1 < argc) {
            std::string v = argv[++i];
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (!item.empty()) out.locks.push_back(item);
            }
        } else if (a == "-r" && i + 1 < argc) {
            out.runTask = argv[++i];
        } else if (a == "-n" && i + 1 < argc) {
            out.repeats = std::atoi(argv[++i]);
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
            if (p > 0) out.cpuParallelIters = p; else out.cpuParallelIters = 2048;
            if (l > 0) out.cpuLockedIters = l; else if (l == -1) {/* keep default */}
        } else if (a == "--csv-only") {
            out.csvOnly = true;
        } else if (a == "--csv-file" && i + 1 < argc) {
            out.csvFile = argv[++i];
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown or incomplete option: " << a << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    if (out.duration <= 0.0) out.duration = 1.0;
    if (out.repeats <= 0) out.repeats = 1;
    // 允许 cpu_burn / do_nothing；其他任务名暂不支持
    if (!(out.runTask == "cpu_burn" || out.runTask == "do_nothing")) {
        std::cerr << "Unsupported task: " << out.runTask << ", supported: cpu_burn, do_nothing" << "\n";
        return false;
    }
    if (out.locks.empty()) {
        std::cerr << "Locks list (-L) is required" << "\n";
        return false;
    }
    if (out.threadBins.empty()) {
        std::cerr << "Thread bins (-B) is required" << "\n";
        return false;
    }
    if (out.csvFile.empty()) {
        std::cerr << "--csv-file is required" << "\n";
        return false;
    }
    return true;
}

static std::vector<int> parse_bins(const std::string& spec) {
    std::vector<int> res;
    if (spec.empty()) return res;
    size_t i = 0;
    auto trim = [](std::string& s){
        size_t b = s.find_first_not_of(" \t");
        size_t e = s.find_last_not_of(" \t");
        if (b == std::string::npos) { s.clear(); return; }
        s = s.substr(b, e - b + 1);
    };
    while (i <= spec.size()) {
        size_t j = spec.find_first_of(",;", i);
        std::string tok = spec.substr(i, (j == std::string::npos ? spec.size() : j) - i);
        trim(tok);
        if (!tok.empty()) {
            size_t dash = tok.find('-');
            size_t colon = tok.find(':');
            if (dash == std::string::npos) {
                int v = std::atoi(tok.c_str());
                if (v > 0) res.push_back(v);
            } else {
                int a = std::atoi(tok.substr(0, dash).c_str());
                int b = 0; int step = 1;
                if (colon == std::string::npos) {
                    b = std::atoi(tok.substr(dash + 1).c_str());
                } else {
                    b = std::atoi(tok.substr(dash + 1, colon - dash - 1).c_str());
                    step = std::atoi(tok.substr(colon + 1).c_str());
                }
                if (a > 0 && b > 0 && step > 0 && a <= b) {
                    for (int v = a; v <= b; v += step) res.push_back(v);
                }
            }
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    // dedupe preserve order
    std::vector<int> out;
    for (int v : res) {
        if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    }
    return out;
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

static std::unique_ptr<iRunTask> make_task(const std::string& task, int cpuParallelIters, int cpuLockedIters) {
    if (task == "cpu_burn") {
        int p = (cpuParallelIters > 0) ? cpuParallelIters : 2048;
        int l = (cpuLockedIters > 0) ? cpuLockedIters : 32;
        return std::make_unique<CpuBurnTask>(p, l);
    }
    if (task == "do_nothing") {
        return std::make_unique<DoNothingTask>();
    }
    return nullptr; // 如需扩展新任务，在此注册
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    // 构造线程数列表（仅 -B）
    std::vector<int> threadCounts = parse_bins(args.threadBins);
    if (threadCounts.empty()) {
        std::cerr << "Invalid -B bins spec results in empty thread set" << "\n";
        return 4;
    }
    // 锁列表（仅 -L）
    std::vector<std::string> lockKinds = args.locks;

    // CSV 输出准备
    // 打开 CSV 文件（必需）
    std::ofstream csvFileOut(args.csvFile, std::ios::out | std::ios::trunc);
    if (!csvFileOut) {
        std::cerr << "Failed to open CSV file: " << args.csvFile << "\n";
        return 5;
    }
    std::ostream* csvOut = &csvFileOut;
    (*csvOut) << "task,lock,threads,duration,repeats,cpu_parallel_iters,cpu_locked_iters,avg_ops,ops_s" << '\n';

    if (!args.csvOnly) {
        std::cout.setf(std::ios::fixed); std::cout.precision(2);
        std::cout << "Task: " << args.runTask
                  << ", Duration: " << args.duration << " s"
                  << ", Repeats: " << args.repeats << "\n";
    }

    auto avg = [](const std::vector<std::uint64_t>& v) {
        long double s = 0; for (auto x : v) s += x; return v.empty() ? 0.0 : static_cast<double>(s / v.size());
    };

    for (const auto& lk : lockKinds) {
        if (!args.csvOnly) {
            std::cout << "\n";
            std::cout << "Lock: " << lk << "\n";
            std::cout << std::left << std::setw(10) << "Threads"
                      << std::right << std::setw(20) << "Avg Ops"
                      << std::setw(20) << "Ops/s" << "\n";
            std::cout << std::string(50, '-') << "\n";
        }
        for (int tc : threadCounts) {
            auto lock = make_lock(lk);
            if (!lock) {
                std::cerr << "Unknown lock kind: " << lk << "\n";
                return 2;
            }
            auto task = make_task(args.runTask, args.cpuParallelIters, args.cpuLockedIters);
            if (!task) {
                std::cerr << "Failed to create task: " << args.runTask << "\n";
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

            if (!args.csvOnly) {
                std::cout << std::left << std::setw(10) << tc
                          << std::right << std::setw(20) << avg_lock_ops
                          << std::setw(20) << lock_qps << "\n";
            }
            int p = (args.runTask == "cpu_burn") ? ((args.cpuParallelIters > 0) ? args.cpuParallelIters : 2048) : 0;
            int l = (args.runTask == "cpu_burn") ? ((args.cpuLockedIters > 0) ? args.cpuLockedIters : 32) : 0;
            (*csvOut) << args.runTask << ',' << lk << ',' << tc << ','
                      << args.duration << ',' << args.repeats << ','
                      << p << ',' << l << ','
                      << std::fixed << std::setprecision(2) << avg_lock_ops << ','
                      << std::fixed << std::setprecision(2) << lock_qps
                      << '\n';
        }
    }
    return 0;
}
