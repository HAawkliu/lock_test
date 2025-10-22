# lock_test

一个用于对比不同锁实现与原子操作性能的简单 C++ 框架。

## 构建

要求：CMake ≥ 3.12，g++/clang++（支持 C++17），pthread（Linux 上默认提供）。

```fish
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

生成的可执行文件位于 `build/lock_test`。

## 运行

参数：
- `-t <threads>` 线程数（默认 4）
- `-r <task>` 任务名称：`sum`
- `-l <lock>` 锁类型：`mutex`

示例：

```fish
./build/lock_test -t 4 -r sum -l mutex
```

程序按固定时长运行（默认 2.0 秒，每轮，次数可通过 -d 指定），并统计吞吐量：
- 使用锁的竞争执行（lock）：临界区不做任何事，仅测量锁/解锁开销

程序输出平均操作数与 ops/s（吞吐量）。

## 扩展

- 新增锁实现：继承 `iLock`，实现 `lock()/unlock()`，在 `main.cpp` 的 `make_lock()` 中注册新名称。
- 新增任务：继承 `iRunTask`，实现 `reset()/run()/name()`，在 `main.cpp` 的 `make_task()` 中注册。

代码结构：

- `include/iLock.h`：锁接口与 RAII 守卫
- `include/locks/StdMutexLock.h`：基于 `std::mutex` 的锁
- `include/iRunTask.h`：任务接口与 `DoNothingTask`
- `src/lockTestSys.h/.cpp`：固定时长测试系统，提供 `run_test`（返回总操作数）
- `src/main.cpp`：参数解析与多轮测试、输出吞吐量

