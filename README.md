# lock_test

一个用于对比不同锁实现吞吐量（ops/s）的轻量 C++17 基准框架。程序在固定时长内启动多线程，在线程间竞争同一把锁，并在临界区执行最小任务以评估不同锁在不同并发度下的表现。

当前支持的锁：
- StdMutexLock（std::mutex）
- TasSpinlock（test-and-set 自旋锁）
- TicketLock（票据锁，公平 FIFO）
- McsLock（MCS 队列锁，较好的可扩展性）

当前支持的任务：
- do_nothing：临界区不做任何事情，用以近似隔离锁本身开销
- cpu_burn：大部分计算在锁外并行执行，锁内保留少量临界区计算，模拟更贴近真实应用的混合负载

备注：本项目不包含“仅原子操作”的无锁对照路径，所有统计均在锁保护路径下完成。

## 构建

要求：
- CMake ≥ 3.12
- g++/clang++，支持 C++17
- pthread（Linux 上默认提供）

建议以 Release 构建（影响性能结果）：

```fish
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

生成的可执行文件位于 `build/lock_test`。

## 使用与示例

用法（与程序内 `--help` 一致）：

```
Usage: ./lock_test [-t threads] [-r task] [-l lock] [-d seconds] [-R p[:l]]
	-t threads   number of pthreads (default runs 1,2,4,8,16,32 when omitted)
	-r task      runtask name: do_nothing (default)
	-l lock      lock kind: mutex (default)
	-d seconds   duration per run in seconds (default 2.0)
	-R p[:l]     cpu_burn iters: parallel p, locked l (default p=2048,l=32)
```

说明：
- 若未指定 `-t`，程序会依次测试线程数 `{1, 2, 4, 8, 16, 32}`。
- 每个线程数组合默认重复运行 5 次取平均（源码中 `repeats=5`）。
- 锁名称支持：`mutex`、`tas|spin|tas_spin`、`ticket`、`mcs`。
- 任务名称支持：`do_nothing`、`cpu_burn`。

典型运行：

```fish
# 仅指定锁，自动跑 1,2,4,8,16,32 线程
./build/lock_test -l mutex

# 指定任务与锁，并固定线程数为 8
./build/lock_test -r cpu_burn -l ticket -t 8

# 延长每组测试时长为 5 秒
./build/lock_test -l mcs -d 5

# 指定 cpu_burn 的并行/加锁迭代（例如 p=1024, l=16）
./build/lock_test -r cpu_burn -l spin -R 1024:16
```

输出格式（数值仅示意）：

```
Task: do_nothing, Lock: mutex, Duration: 2.00 s, Repeats: 5

Threads                Avg Ops               Ops/s
--------------------------------------------------
1                      1,234,567.00          617,283.50
2                      2,345,678.00        1,172,839.00
...
```

`Avg Ops` 为该线程数组合下的平均操作次数（多次重复取平均），`Ops/s` 为 `Avg Ops / duration`。

## 目录结构

- `include/`
	- `iLock.h`：锁接口与 `LockGuard`
	- `iRunTask.h`：任务接口（两阶段：`run_parallel()` 与 `run_locked()`）与内置任务 `DoNothingTask`、`CpuBurnTask`
	- `locks/`
		- `StdMutexLock.h`：基于 `std::mutex`
		- `TasSpinlock.h`：基于 `std::atomic_flag` 的 TAS 自旋锁
		- `TicketLock.h`：公平票据锁
		- `McsLock.h`：MCS 队列锁（每线程节点、传递式解锁）
	- `utils/CycleTimer.h`：跨平台计时工具（x86_64 上使用 TSC）
- `src/`
	- `main.cpp`：参数解析、构造锁与任务、按线程数组合循环测试并输出表格
	- `lockTestSys.h/.cpp`：核心测试器，创建 `pthread` 线程，在截止时间前竞争同一把锁并累计操作次数

## 扩展指南

新增自定义锁：
1. 新建类继承 `lt::iLock`，实现 `lock()/unlock()`。
2. 在 `src/main.cpp` 的 `make_lock()` 中为你的锁添加名称分支。

新增自定义任务：
1. 新建类继承 `lt::iRunTask`，实现 `reset()/run_parallel()/run_locked()/name()`。
2. 在 `src/main.cpp` 的 `make_task()` 中注册任务名称。

建议：
- 保持任务在临界区内的状态最小化，以避免缓存争用对锁结果的干扰。
- 如需评估更重负载，可按需增加 `CpuBurnTask` 的计算迭代或实现新的任务类型。

## 实现要点与注意事项

- 计时：`CycleTimer` 在 x86_64 上使用 `rdtsc` 推导秒；其他平台使用 `clock_gettime`。不同平台时间源精度不同，数值仅供相对比较。
- 线程：使用 `pthread_create/join` 创建与回收线程。
- 减少伪共享：每个线程的结果存放在独占一条 cache line 的 `ThreadResult` 中。
- 公平性与扩展性：Ticket/MCS 提供公平或更好的扩展性；TAS 在高竞争下可能退化。
- 结果稳定性：请在较空闲、固定频率或性能模式稳定的环境中运行，并使用 Release 构建。

## 故障排查

- 链接错误 `-lpthread`：确认已通过 CMake 的 `Threads::Threads` 链接（本项目已配置），不要手动移除。
- 性能异常偏低：检查是否为 Debug 构建；关闭多余后台任务；必要时延长 `-d`。
- 运行输出为空或异常：使用 `-h`/`--help` 检查参数；确认锁或任务名称拼写正确。

