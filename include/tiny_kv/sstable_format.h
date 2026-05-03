#ifndef TINY_KV_SSTABLE_FORMAT_H_
#define TINY_KV_SSTABLE_FORMAT_H_

#include <cstdint>
#include <cstring>

#include "types.h"

namespace tiny_kv {

    // SSTable 数据块目标大小（4KB，压缩前）
    inline constexpr uint32_t kSSTableBlockSize = 4096;

    // 文件标识
    inline constexpr uint32_t kSSTableMagic = 0xC0DEBEEF;
    inline constexpr uint32_t kSSTableVersion = 1;

    // ============================================================
    // DataBlock 条目格式: [key_len(4B)|value_len(4B)|key|value]
    // SSTable 只存放有效 Put 记录，已删除的 key 直接不写入
    // ============================================================

    // 编码一条 KV 到 DataBlock 的线上格式
    inline void EncodeDataEntry(std::string& dst, const Key& key, const Value& value) {
        uint32_t key_len = static_cast<uint32_t>(key.size());
        uint32_t val_len = static_cast<uint32_t>(value.size());
        dst.append(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        dst.append(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        dst.append(key);
        dst.append(value);
    }

    // 从缓冲区解码一条 DataBlock 条目，成功返回 true 并设置 *consumed
    inline bool DecodeDataEntry(const char* data, size_t len, size_t* consumed,
                                Key* key, Value* value) {
        if (len < sizeof(uint32_t) * 2) return false;

        uint32_t key_len, val_len;
        std::memcpy(&key_len, data, sizeof(key_len));
        std::memcpy(&val_len, data + sizeof(key_len), sizeof(val_len));

        size_t needed = sizeof(uint32_t) * 2 + key_len + val_len;
        if (len < needed) return false;

        key->assign(data + sizeof(uint32_t) * 2, key_len);
        value->assign(data + sizeof(uint32_t) * 2 + key_len, val_len);
        *consumed = needed;
        return true;
    }

    // ============================================================
    // IndexBlock 条目
    // 线上格式: [last_key_len(4B)|last_key|block_offset(8B)|block_size(4B)]
    // 每条记录对应一个 DataBlock，用于二分定位
    // ============================================================

    struct IndexEntry {
        Key      last_key;  // 该 DataBlock 内最大的 key
        uint64_t offset;    // DataBlock 在文件中的偏移
        uint32_t size;      // DataBlock 的字节大小
    };

    // 编码一条 IndexBlock 条目到线上格式
    inline void EncodeIndexEntry(std::string& dst, const IndexEntry& entry) {
        uint32_t key_len = static_cast<uint32_t>(entry.last_key.size());
        dst.append(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        dst.append(entry.last_key);
        dst.append(reinterpret_cast<const char*>(&entry.offset), sizeof(entry.offset));
        dst.append(reinterpret_cast<const char*>(&entry.size), sizeof(entry.size));
    }

    // 从缓冲区解码一条 IndexBlock 条目
    inline bool DecodeIndexEntry(const char* data, size_t len, size_t* consumed,
                                 IndexEntry* entry) {
        if (len < sizeof(uint32_t)) return false;

        uint32_t key_len;
        std::memcpy(&key_len, data, sizeof(key_len));

        size_t needed = sizeof(uint32_t) + key_len + sizeof(uint64_t) + sizeof(uint32_t);
        if (len < needed) return false;

        size_t offset = sizeof(uint32_t);
        entry->last_key.assign(data + offset, key_len);
        offset += key_len;
        std::memcpy(&entry->offset, data + offset, sizeof(entry->offset));
        offset += sizeof(entry->offset);
        std::memcpy(&entry->size, data + offset, sizeof(entry->size));
        *consumed = needed;
        return true;
    }

    // ============================================================
    // Footer: 文件末尾固定 24 字节，定位 IndexBlock + 校验完整性
    // ============================================================

    struct Footer {
        uint64_t index_offset;  // IndexBlock 在文件中的字节偏移
        uint64_t index_count;   // IndexEntry 个数
        uint32_t magic;         // kSSTableMagic
        uint32_t version;       // kSSTableVersion
    };

    static_assert(sizeof(Footer) == 24, "Footer must be 24 bytes");

}  // namespace tiny_kv

#endif
