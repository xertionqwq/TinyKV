# Tiny KV

简易 KV 存储引擎，基于 My_STL 跳表实现内存表，WAL + SSTable 双层持久化。C++17，CMake 构建。

```cpp
#include "tiny_kv/db.h"
tiny_kv::DB db;
db.Open("/tmp/mydb");
db.Put("dog", "1");
db.Put("cat", "2");
db.Delete("cat");
tiny_kv::Value v;
db.Get("dog", &v);  // "1"
db.Close();
```

## 快速开始

```bash
cd build && cmake .. -DTINY_KV_BUILD_TESTS=ON && make -j$(nproc)
./tests/test_db          # DB 测试（10 用例）
./tests/test_kv_store    # KVStore 测试（15 用例）
./tests/test_sstable     # SSTable 测试（10 用例）
./tests/test_thread_pool # ThreadPool 测试（5 用例）
```

编译选项：`-Wall -Wextra -g -fsanitize=address`。

依赖项目 [My_STL](https://github.com/xertion/My_STL)，通过 CMake `FetchContent` 引用，无需手动安装。

## 项目结构

```
include/tiny_kv/
├── types.h              # 公共类型定义（Key/Value/Status/RecordType/Tombstone）
├── kv_store.h           # KVStore: 跳表封装，核心 KV 接口
├── wal.h                # WAL: 异步预写日志，后台线程写盘
├── sstable_format.h     # SSTable 磁盘格式（DataBlock/IndexBlock/Footer 编解码）
├── sstable_builder.h    # SSTableBuilder: 内存快照序列化到磁盘
├── sstable_reader.h     # SSTableReader: 磁盘 SSTable 二分查找，无锁并发读
├── thread_pool.h        # ThreadPool: 通用线程池
└── db.h                 # DB: 用户入口，协调 KVStore + WAL + SSTable
src/
├── kv_store.cpp         # KVStore 实现
├── wal.cpp              # WAL 异步实现（序列化入队 + 后台线程 write）
├── sstable_builder.cpp  # SSTableBuilder 实现
├── sstable_reader.cpp   # SSTableReader 实现
├── thread_pool.cpp      # ThreadPool 实现
└── db.cpp               # DB 实现
tests/
├── test_kv_store.cpp    # 15 用例
├── test_sstable.cpp     # 10 用例
├── test_thread_pool.cpp # 5 用例
├── test_db.cpp          # 10 用例
└── CMakeLists.txt
```

## 实现进度

| 模块 | 状态 |
|------|------|
| KVStore（Put/Get/Delete/Exists/ForEach/Clear） | ✅ |
| WAL（异步写入，后台线程 write + fsync，Replay 崩溃恢复） | ✅ |
| SSTable 磁盘格式（DataBlock/IndexBlock/Footer 编解码） | ✅ |
| SSTableBuilder（内存快照落盘） | ✅ |
| SSTableReader（磁盘文件二分查找，无锁并发读） | ✅ |
| ThreadPool（通用线程池，Engine 异步 dump 用） | ✅ |
| DB（协调 KVStore + WAL + SSTable，统一读写接口） | ✅ |
| Compaction（合并 SSTable，清理 tombstone） | ⬜ |
| Socket 通信 + epoll | ⬜ |

## 设计决策

### 内存表：为什么用跳表而非红黑树

跳表的 `ForEach` 天然有序遍历（O(n) 走 level-0 链表），dump 到 SSTable 时无需额外排序。红黑树的中序遍历需要栈或 parent 指针，实现更重，且跳表并发改造（lock-free read）比红黑树更简单。

My_STL 的 `skip_list<K,V>` 已经提供了线程安全保证：读操作无锁（原子 load），写操作通过 `std::mutex` 串行化但分配在锁外完成（乐观分配）。

### 持久化：为什么 WAL + SSTable 双层

|  | WAL | SSTable |
|--|-----|---------|
| 写入方式 | 追加（append） | 全量快照（rewrite） |
| 写入频率 | 每条 Put/Delete 都写 | memtable 超 4MB 阈值触发 |
| 作用 | 防止崩溃丢失最新写入 | 压缩历史数据，加速重启 |

WAL 保证写入即时落盘（O_SYNC），断电不丢数据。SSTable 将内存快照压缩为有序文件，重启时只需回放 WAL 中最后一段增量日志，而非整个历史。

### WAL 异步化：为什么用专用线程而非 ThreadPool

WAL 写入必须严格有序，单一后台线程 + 内部队列天然保证顺序。若放入线程池，多个工作线程并发 write 同一 fd 会打乱记录顺序，崩溃恢复无法回放。

异步流程：`PutRecord` 在调用方序列化整条记录 `[Type|KeyLen|ValueLen|Key|Value]` 并入队即返回，后台线程出队 write。`Sync()` 等待队列排空 + `fsync`。

### DB 层：为什么 memtable 冻结而非原地 dump

```
Put 路径:  WAL → mem_ (可写)
            ↓ 超 4MB
           mem_ → imm_ (只读) → ThreadPool 异步 dump
           mem_ = new KVStore (新可写)
```

切换是 `shared_ptr` 交换（纳秒级 `unique_lock` 临界区），后续 Put 立即写入新 mem_，不阻塞。正在 dump 的旧表由 `shared_ptr` 保活，读路径通过 `imm_` 继续访问，dump 完成后自动释放。

### DB 层并发：为什么用 shared_mutex 而非普通 mutex

| 操作 | 锁类型 | 临界区 |
|------|--------|--------|
| Get | `shared_lock` | 读 mem_/imm_ 指针 |
| Put/Delete | `unique_lock` | 仅 swap mem_/imm_ 时持有 |

多个 Get 可以并发执行，Put/Delete 与 Get 仅在指针交换的瞬间互斥。SSTable 列表用独立 `mutex` 保护，不阻塞读路径。

### Delete 语义：为什么用 tombstone 而非真删

`KVStore::Delete` 从跳表中真正移除 key，但旧 SSTable 中可能还有该 key 的旧值。Get 查 memtable 找不到，退到 SSTable 就会读到已删除的旧数据——这是错误的。

解决方案：`DB::Delete` 不调用 `KVStore::Delete`，而是 `Put(key, kTombstoneValue)`。Get 读到 tombstone 时返回 NotFound。tombstone 随 memtable dump 进入 SSTable，持续屏蔽旧数据，直到 compaction 清理。

已知限制：用户不能存储值为 `"\x01"`（SOH 字符）的数据。compaction 后此限制解除。

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

### SSTableReader：为什么无锁并发安全

`Open()` 之后对象只读：
- `index_entries_`（`vector<IndexEntry>`）不可变
- `fd_` 配合 `pread()`——`pread` 不修改文件偏移量，操作系统保证线程安全
- 多读线程并发调用 `Get()` 无需任何 mutex

## 格式约定

- 命名空间：`tiny_kv`，命名空间内额外缩进一个 tab
- 类名：PascalCase（`KVStore`、`WAL`、`SSTableBuilder`、`ThreadPool`、`DB`）
- 函数名：PascalCase（`Put`、`Get`、`Delete`、`Open`、`Close`、`Submit`、`Shutdown`）
- 文件名：snake_case（`kv_store.h`、`wal.cpp`、`thread_pool.h`）
- 常量：kCamelCase（`kSSTableMagic`、`kSSTableBlockSize`、`kTombstoneValue`）
- My_STL 类型使用 `MySTL::` 命名空间前缀
- 所有的 `{` 前面空一格，紧跟当前行：`class DB {`、`if (v) {`

## 已知限制

- **DB 多线程**：读路径多线程安全（shared_mutex），写路径串行化（memtable 内部 mutex）
- **Tombstone 值占用**：`"\x01"` 作为墓碑标记，用户不可存储此值（compaction 后解除）
- **WAL 无压缩**：删除操作写入墓碑记录（Delete tombstone），不回收空间，依赖 SSTable dump 清理
- **SSTable 无压缩**：DataBlock 为原始 KV 对，未做 snappy/zstd 压缩
- **SSTable 不可变**：写入后只读不修改，合并（compaction）需要新建文件
- **无 compaction**：多次 Put + Delete 后 SSTable 中 tombstone 累积，需后续实现合并清理

## 参考资源

- [LevelDB](https://github.com/google/leveldb) — WAL + MemTable + SSTable 双层持久化架构
- [My_STL](https://github.com/xertionqwq/MySTL) — 跳表底层实现
- 侯捷《STL 源码剖析》— 空间配置器与迭代器萃取
