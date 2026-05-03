#ifndef TINY_KV_TYPES_H_
#define TINY_KV_TYPES_H_

#include <cstdint>
#include <string>

namespace tiny_kv {

// 此文件是项目的全局类型定义中枢，所有模块（KVStore/WAL/SSTable）共享

    // 基础 KV 类型
    using Key = std::string;
    using Value = std::string;

    // 操作状态码，KVStore / WAL / SSTable 全体复用此枚举
    enum class Status : uint8_t {
        OK = 0,
        NotFound = 1,
        Corruption = 2,
        IOError = 3,
        InvalidArgument = 4,
    };

    // WAL 磁盘记录类型：Put 写入数据，Delete 写入墓碑
    enum class RecordType : uint8_t {
        Put = 0,
        Delete = 1,
    };

    constexpr const char* kWALFileName = "WAL";

    constexpr size_t kDefaultMemTableSize = 4 * 1024 * 1024;  // 4MB

    inline std::string SSTableFileName(const std::string& db_path, uint64_t number) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s/%06lu.sst", db_path.c_str(), number);
        return buf;
    }

}  // namespace tiny_kv

#endif
