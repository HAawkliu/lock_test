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

默认迭代次数为 20,000,000。程序会多次（默认 5 次）运行两组测试：
- 使用锁的竞争执行（lock）
- 仅用原子操作的执行（atomic）

最后输出两者的平均耗时（微秒）及比值。

## 扩展

- 新增锁实现：继承 `iLock`，实现 `lock()/unlock()`，在 `main.cpp` 的 `make_lock()` 中注册新名称。
- 新增任务：继承 `iRunTask`，实现 `reset()/run()/name()`，在 `main.cpp` 的 `make_task()` 中注册。

代码结构：

- `include/iLock.h`：锁接口与 RAII 守卫
- `include/locks/StdMutexLock.h`：基于 `std::mutex` 的锁
- `include/iRunTask.h`：任务接口与 `SumTask`
- `src/lockTestSys.h/.cpp`：测试系统，提供 `run_test` 与 `run_atomic`
- `src/main.cpp`：参数解析与多轮测试

