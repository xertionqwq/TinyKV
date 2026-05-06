#ifndef TINY_KV_SSTABLE_BUILDER_H_
#define TINY_KV_SSTABLE_BUILDER_H_

#include <string>
#include <vector>

#include "kv_store.h"
#include "sstable_format.h"
#include "types.h"

namespace tiny_kv {

    class SSTableBuilder {
        public:
            SSTableBuilder() = default;

            // 将 store 的所有 KV 对序列化为 SSTable 写入 filepath
            Status Build(const KVStore& store, const std::string& filepath);

        private:
            // 将当前累积的 DataBlock 写入文件，记录索引条目
            Status FlushBlock(int fd);

            std::string           cur_block_{};       // 当前 DataBlock 缓冲区
            std::string           cur_last_key_{};    // cur_block_ 中最大的 key
            std::vector<IndexEntry> index_entries_{}; // 已刷出的 Block 索引
            uint64_t              file_offset_ = 0; // 当前文件写入偏移
        };

}  // namespace tiny_kv

#endif
