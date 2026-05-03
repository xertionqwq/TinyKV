# Tiny KV

简易 KV 存储引擎，基于 My_STL 跳表实现内存表，WAL + SSTable 双层持久化。C++17，CMake 构建。

```cpp
#include "tiny_kv/kv_store.h"
tiny_kv::KVStore store;
store.Put("dog", "1");
store.Put("cat", "2");
store.Delete("cat");
```

## 快速开始

```bash
cd build && cmake .. -DTINY_KV_BUILD_TESTS=ON && make -j$(nproc)
./tests/test_kv_store                     # KVStore 测试（15 用例）
./tests/test_sstable                      # SSTable 测试（10 用例）
# 或一键: bash scripts/run_tests_kv_store.sh / run_tests_sstable.sh
```

编译选项：`-Wall -Wextra -g -fsanitize=address`。

依赖项目 [My_STL](https://github.com/xertion/My_STL)，通过 CMake `FetchContent` 引用，无需手动安装。

## 项目结构

```
include/tiny_kv/
├── types.h              # 公共类型定义（Key/Value/Status/RecordType）
├── kv_store.h           # KVStore: 跳表封装，核心 KV 接口
├── wal.h                # WAL: 极简预写日志，崩溃恢复回放
├── sstable_format.h     # SSTable 磁盘格式（DataBlock/IndexBlock/Footer 编解码）
├── sstable_builder.h    # SSTableBuilder: 内存快照序列化到磁盘
└── sstable_reader.h     # SSTableReader: 磁盘 SSTable 二分查找
src/
├── kv_store.cpp         # KVStore 实现
├── wal.cpp              # WAL 实现（writev + O_SYNC）
├── sstable_builder.cpp  # SSTableBuilder 实现
└── sstable_reader.cpp   # SSTableReader 实现
tests/
├── test_kv_store.cpp    # 15 用例
├── test_sstable.cpp     # 10 用例
└── CMakeLists.txt
scripts/
├── run_tests_kv_store.sh
└── run_tests_sstable.sh
```

## 实现进度

| 模块 | 状态 |
|------|------|
| KVStore（Put/Get/Delete/Exists/ForEach/Clear） | ✅ |
| WAL（writev + O_SYNC 追加，Replay 崩溃恢复） | ✅ |
| SSTable 磁盘格式（DataBlock/IndexBlock/Footer 编解码） | ✅ |
| SSTableBuilder（内存快照落盘） | ✅ |
| SSTableReader（磁盘文件二分查找） | ✅ |
| Engine（协调 KVStore + WAL + SSTable，统一读写接口） | ⬜ |
| 线程池异步刷盘 | ⬜ |
| Socket 通信 + epoll | ⬜ |

## 设计决策

### 内存表：为什么用跳表而非红黑树

跳表的 `ForEach` 天然有序遍历（O(n) 走 level-0 链表），dump 到 SSTable 时无需额外排序。红黑树的中序遍历需要栈或 parent 指针，实现更重，且跳表并发改造（lock-free read）比红黑树更简单。

My_STL 的 `skip_list<K,V>` 已经提供了线程安全保证：读操作无锁（原子 load），写操作通过 `std::mutex` 串行化但分配在锁外完成（乐观分配）。

### 持久化：为什么 WAL + SSTable 双层

|  | WAL | SSTable |
|--|-----|---------|
| 写入方式 | 追加（append） | 全量快照（rewrite） |
| 写入频率 | 每条 Put/Delete 都写 | 定时或 WAL 达阈值触发 |
| 作用 | 防止崩溃丢失最新写入 | 压缩历史数据，加速重启 |

WAL 保证写入即时落盘（O_SYNC），断电不丢数据。SSTable 将内存快照压缩为有序文件，重启时只需回放 WAL 中最后一段增量日志，而非整个历史。

### SSTable 文件格式：为什么分三层

```
┌──────────┬──────────┬─────┬─────────────┬──────────┐
│DataBlock0│DataBlock1│ ... │ IndexBlock  │  Footer  │
│  (4KB)   │  (4KB)   │     │  (变长)      │ (固定24B)│
└──────────┴──────────┴─────┴─────────────┴──────────┘
```

- **DataBlock**（4KB 固定块）：存 KV 对，一块一次磁盘 IO
- **IndexBlock**（变长）：每条记录 `[last_key, offset, size]`，二分定位目标 Block
- **Footer**（24B 固定尾）：`[index_offset, index_count, magic, version]`，读文件的唯一入口

对比单文件全量扫描：三层结构将磁盘查找从 O(n) 降到 O(log n)，且每次只读 4KB。

### SSTable Get：为什么二分 IndexBlock 而不是全扫

IndexBlock 中每条记录对应一个 DataBlock，记录了该 Block 的 `last_key`。查找时 `std::lower_bound` 定位目标 Block，一次 `pread` 读取该 Block，内部线性扫描。

Block 内线性扫描（而非 Block 内再建索引）是刻意简化——4KB 只够存几十到上百条 KV，线性扫描的 CPU 开销远小于额外的磁盘 IO。

### SSTableReader：为什么无锁并发安全

`Open()` 之后对象只读：
- `index_entries_`（`vector<IndexEntry>`）不可变
- `fd_` 配合 `pread()`——`pread` 不修改文件偏移量，操作系统保证线程安全
- 多读线程并发调用 `Get()` 无需任何 mutex

### WAL 写入：为什么用 writev 而非多次 write

每条 WAL 记录包含 `[Type|KeyLen|ValueLen|Key|Value]` 五个字段。`writev` 将前四个字段（定长头 + key）合并为一次系统调用，减少内核态切换。Value 单独 `write` 是因为其长度可变且可能较大，合并到 iovec 不带来额外收益。

### KVStore Put 返回值：为什么重复 key 返回 false

```cpp
bool Put(const Key& key, const Value& value);  // true=新增 false=覆盖
```

区别于"无脑返回 true"的简化设计，返回 `false` 表示覆盖语义。调用方可以据此判断是否需要更新 WAL 中的记录类型（Put vs Delete tombstone），以及是否需要触发 SSTable 压缩。

## 格式约定

- 命名空间：`tiny_kv`，命名空间内额外缩进一个 tab
- 类名：PascalCase（`KVStore`、`WAL`、`SSTableBuilder`）
- 函数名：PascalCase（`Put`、`Get`、`Delete`、`Open`、`Close`）
- 文件名：snake_case（`kv_store.h`、`wal.cpp`）
- 常量：kCamelCase（`kSSTableMagic`、`kSSTableBlockSize`）
- My_STL 类型使用 `MySTL::` 命名空间前缀
- 所有的 `{` 前面空一格，紧跟当前行：`class KVStore {`、`if (v) {`

## 已知限制

- **单线程**：KVStore / WAL / SSTable 均为单线程设计，Engine 层规划中
- **WAL 无压缩**：删除操作写入墓碑记录（Delete tombstone），不回收空间，依赖后续 SSTable dump 清理
- **SSTable 无压缩**：DataBlock 为原始 KV 对，未做 snappy/zstd 压缩
- **内存不设限**：KVStore 的跳表大小无上限，需 Engine 层通过 `kDefaultMemTableSize`（4MB）阈值触发 dump
- **SSTable 不可变**：写入后只读不修改，合并（compaction）需要新建文件

## 参考资源

- [LevelDB](https://github.com/google/leveldb) — WAL + MemTable + SSTable 双层持久化架构
- [My_STL](https://github.com/xertionqwq/MySTL) — 跳表底层实现
- 侯捷《STL 源码剖析》— 空间配置器与迭代器萃取
