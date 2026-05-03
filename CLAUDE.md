# CLAUDE.md

## 项目概述
- 简易 KV 存储引擎，基于 My_STL 跳表实现内存表，WAL + SSTable 双层持久化
- C++17，CMake 构建，静态库
- 项目位于 /home/xertion/Code/Tiny_KV
- 依赖项目 /home/xertion/Code/My_STL（通过 FetchContent 引用）

## 当前状态
- `KVStore`：跳表封装，Put/Get/Delete/Exists/Clear/ForEach
- `WAL`：预写日志，异步写入（后台写线程），支持崩溃恢复回放
- `SSTableBuilder`：内存快照序列化到磁盘（DataBlock/IndexBlock/Footer 三层结构）
- `SSTableReader`：磁盘 SSTable 二分查找，无锁并发读
- `ThreadPool`：通用线程池，Engine 层异步 dump SSTable 用
- 测试: KVStore 15/15, SSTable 10/10, ThreadPool 5/5 全部通过
- 编译通过，零警告。My_STL 跳表原有测试 20/20 通过

## 架构
```
include/tiny_kv/
  types.h              — Key/Value/Status/RecordType 公共类型
  kv_store.h           — KVStore: skip_list 封装
  wal.h                — WAL: 异步预写日志，后台线程写盘
  sstable_format.h     — SSTable 磁盘格式（DataBlock/IndexBlock/Footer 编解码）
  sstable_builder.h    — SSTableBuilder: 内存快照序列化到磁盘
  sstable_reader.h     — SSTableReader: 磁盘 SSTable 二分查找
  thread_pool.h        — ThreadPool: 通用线程池
src/
  kv_store.cpp         — KVStore 实现
  wal.cpp              — WAL 实现（异步：序列化入队 + 后台线程 write + fsync）
  sstable_builder.cpp  — SSTableBuilder 实现
  sstable_reader.cpp   — SSTableReader 实现
  thread_pool.cpp      — ThreadPool 实现
tests/
  test_kv_store.cpp    — 15 用例
  test_sstable.cpp     — 10 用例
  test_thread_pool.cpp — 5 用例
```

## 后续规划
1. Engine 层（协调 KVStore + WAL + SSTable，统一 Get/Put/Delete 接口）
2. 后台 compaction（合并 SSTable 文件，回收 Delete tombstone 空间）
3. Socket 通信 + epoll

## 格式约定
- 命名空间: `tiny_kv`
- 命名空间内额外缩进一个 tab（参考 kv_store.h）
- 类名: PascalCase（KVStore, WAL, SSTableBuilder, ThreadPool）
- 函数名: PascalCase（Put, Get, Delete, Open, Close, Submit, Shutdown）
- 文件名: snake_case（kv_store.h, wal.cpp, thread_pool.h）
- 常量: kCamelCase（kSSTableBlockSize, kSSTableMagic）
- My_STL 类型使用 `MySTL::` 命名空间前缀
- 所有的 `{` 前面空一格，紧跟当前行: `class KVStore {`, `if (v) {`

## 构建与测试
```bash
cd build && cmake .. -DTINY_KV_BUILD_TESTS=ON && make -j$(nproc)
./tests/test_kv_store      # KVStore 测试（15 用例）
./tests/test_sstable       # SSTable 测试（10 用例）
./tests/test_thread_pool   # ThreadPool 测试（5 用例）
```
编译选项: `-Wall -Wextra -g -fsanitize=address`，无 `-Werror`

## 协作约定
1. 发现 bug 时，先逐一罗列问题并说明原因，经同意后再修改代码
2. 修改完成后先编译，告知编译结果，经同意后再继续测试或其他操作

## 开发规定
1. 所有涉及内存操作的代码（分配、释放、构造、析构），必须明确说明对象的生命周期和所有权归属
2. 解释架构决策时，必须对比至少两种方案的优缺点，再给出选择理由
3. 任何终端操作（编译、运行测试、文件操作等）前，必须先推演可能引发的系统级副作用
4. 文件 I/O 操作（write/read/fsync）必须检查返回值，失败时清理资源并返回错误状态
