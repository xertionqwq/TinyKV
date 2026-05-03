#ifndef TINY_KV_SSTABLE_READER_H_
#define TINY_KV_SSTABLE_READER_H_

#include <string>
#include <vector>

#include "sstable_format.h"
#include "types.h"

namespace tiny_kv {

    class SSTableReader {
        public:
            SSTableReader() = default;
            ~SSTableReader();

            SSTableReader(const SSTableReader&) = delete;
            SSTableReader& operator=(const SSTableReader&) = delete;

            // 打开 SSTable 文件，读取 Footer + IndexBlock 到内存
            Status Open(const std::string& filepath);

            // 在当前 SSTable 中查找 key，线程安全（多读并发）
            Status Get(const Key& key, Value* value) const;

            Status Close();
            const std::string& Filepath() const { return filepath_; }

        private:
            std::string             filepath_;
            int                     fd_ = -1;
            std::vector<IndexEntry> index_entries_;
        };

}  // namespace tiny_kv

#endif
