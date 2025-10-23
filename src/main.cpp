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
    int threads = 0; // 0 表示未显式指定，触发批量线程数测试
    std::string runTask = "do_nothing";
    std::string lockKind = "mutex"; // 若 -L 指定多个锁，则此字段作为默认/回退
    int repeats = 5; // internal multiple runs for averaging
    double duration = 2.0; // seconds per test run
    bool explicitThreads = false; // 是否由 -t 显式指定
    // cpu_burn 配置（多数并行，少数加锁）
    int cpuParallelIters = -1; // <0 表示采用默认值
    int cpuLockedIters = -1;   // <0 表示采用默认值
    // 线程 sweep 控制
    std::vector<int> threadsList; // -T 指定
    std::string threadBins;       // -B 指定，如 "1-64:1,65-128:8"
    std::vector<std::string> locks; // -L 指定多个锁
    // 输出控制
    bool csv = false;           // 是否输出 CSV 到 stdout
    bool csvOnly = false;       // 仅输出 CSV（不打印表格）
    std::string csvFile;        // 输出 CSV 文件路径，可与 csvOnly 组合
};

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [-t threads] [-T list] [-B bins] [-L locks] [-r task] [-l lock] [-n repeats] [-d seconds] [--csv] [--csv-only] [--csv-file path]" << "\n";
    std::cout << "  -t threads   number of pthreads (default runs 1,2,4,8,16,32 when omitted)\n";
    std::cout << "  -T list      explicit thread list, e.g. 1,3,6,9,12 (overrides -t/-B default sweep)\n";
    std::cout << "  -B bins      piecewise bins like 1-64:1,65-128:8 (inclusive ranges, step default=1)\n";
    std::cout << "  -L locks     comma-separated locks, e.g. mutex,spin,ticket,mcs (default single -l)\n";
    std::cout << "  -r task      runtask name: do_nothing (default)\n";
    std::cout << "  -l lock      lock kind: mutex (default)\n";
    std::cout << "  -n repeats   repeats per thread setting (default 5)\n";
    std::cout << "  -d seconds   duration per run in seconds (default 2.0)\n";
    std::cout << "  -R p[:l]     cpu_burn iters: parallel p, locked l (default p=2048,l=32)\n";
    std::cout << "  --csv        also print CSV lines to stdout (with header)\n";
    std::cout << "  --csv-only   print only CSV (suppress formatted table)\n";
    std::cout << "  --csv-file f write CSV to file path f (will include header; creates/overwrites)\n";
}

static bool parse_args(int argc, char** argv, Args& out) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-t" && i + 1 < argc) {
            out.threads = std::atoi(argv[++i]);
            out.explicitThreads = true;
        } else if (a == "-T" && i + 1 < argc) {
            std::string v = argv[++i];
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (!item.empty()) out.threadsList.push_back(std::atoi(item.c_str()));
            }
            // remove non-positive
            out.threadsList.erase(std::remove_if(out.threadsList.begin(), out.threadsList.end(), [](int x){return x<=0;}), out.threadsList.end());
        } else if (a == "-B" && i + 1 < argc) {
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
        } else if (a == "-l" && i + 1 < argc) {
            out.lockKind = argv[++i];
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
            if (p > 0) out.cpuParallelIters = p;
            if (l > 0) out.cpuLockedIters = l;
        } else if (a == "--csv") {
            out.csv = true;
        } else if (a == "--csv-only") {
            out.csv = true;
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
    if (out.explicitThreads && out.threads <= 0) out.threads = 1;
    if (out.duration <= 0.0) out.duration = 1.0;
    if (out.repeats <= 0) out.repeats = 1;
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
    } else if (!args.threadsList.empty()) {
        threadCounts = args.threadsList;
    } else if (!args.threadBins.empty()) {
        threadCounts = parse_bins(args.threadBins);
        if (threadCounts.empty()) {
            std::cerr << "Invalid -B bins spec results in empty thread set" << "\n";
            return 4;
        }
    } else {
        threadCounts = {1, 2, 4, 8, 16, 32};
    }
    // 锁列表
    std::vector<std::string> lockKinds = args.locks.empty() ? std::vector<std::string>{args.lockKind} : args.locks;

    // CSV 输出准备
    std::ostream* csvOut = nullptr;
    std::ofstream csvFileOut;
    if (args.csv || !args.csvFile.empty()) {
        if (!args.csvFile.empty()) {
            csvFileOut.open(args.csvFile, std::ios::out | std::ios::trunc);
            if (!csvFileOut) {
                std::cerr << "Failed to open CSV file: " << args.csvFile << "\n";
                return 5;
            }
            csvOut = &csvFileOut;
        } else {
            csvOut = &std::cout;
        }
        (*csvOut) << "task,lock,threads,duration,repeats,cpu_parallel_iters,cpu_locked_iters,avg_ops,ops_s" << '\n';
    }

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

            if (!args.csvOnly) {
                std::cout << std::left << std::setw(10) << tc
                          << std::right << std::setw(20) << avg_lock_ops
                          << std::setw(20) << lock_qps << "\n";
            }
            if (csvOut) {
                int p = (args.cpuParallelIters > 0) ? args.cpuParallelIters : 2048;
                int l = (args.cpuLockedIters > 0) ? args.cpuLockedIters : 32;
                (*csvOut) << args.runTask << ',' << lk << ',' << tc << ','
                          << args.duration << ',' << args.repeats << ','
                          << p << ',' << l << ','
                          << std::fixed << std::setprecision(2) << avg_lock_ops << ','
                          << std::fixed << std::setprecision(2) << lock_qps
                          << '\n';
            }
        }
    }
    return 0;
}
