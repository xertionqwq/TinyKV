#ifndef TINY_KV_WAL_H_
#define TINY_KV_WAL_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "types.h"

namespace tiny_kv {

// 预写日志：先写日志再写内存，崩溃恢复时回放
// 磁盘格式: [Type(1B)|KeyLen(4B)|ValueLen(4B)|Key|Value]
// 异步写入：PutRecord/DeleteRecord 序列化后入队即返回，后台线程写盘
class WAL {
public:
    using ReplayCallback = std::function<void(const Key&, const Value&, RecordType)>;

    explicit WAL(const std::string& filepath);
    ~WAL();

    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;

    // 打开 WAL（去掉 O_SYNC，启动后台写线程）
    Status Open();
    // 等待所有已提交记录写入完成 + fsync + 关闭
    Status Close();

    // 非阻塞：序列化后入队即返回
    Status PutRecord(const Key& key, const Value& value);
    Status DeleteRecord(const Key& key);

    // 阻塞等待所有已提交记录写入完成 + fsync
    Status Sync();

    // 回放已有 WAL 中的所有记录（用于崩溃恢复）
    Status Replay(const ReplayCallback& callback);

    const std::string& Filepath() const { return filepath_; }

private:
    static std::string SerializeRecord(RecordType type, const Key& key, const Value& value);
    void WriterThread();

    std::string filepath_;
    int fd_ = -1;

    std::thread writer_thread_;
    std::queue<std::string> write_queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
    int pending_writes_ = 0;
    bool write_error_ = false;
};

}  // namespace tiny_kv

#endif
