#include "tiny_kv/sstable_builder.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

namespace tiny_kv {
    // 写单个 KV 到当前 DataBlock，满了就先 FlushBlock
    Status SSTableBuilder::FlushBlock(int fd) {
        size_t size = cur_block_.size();
        // 将当前 DataBlock 交给内核
        ssize_t written = write(fd, cur_block_.data(), size);
        if (written < 0 || static_cast<size_t>(written) != size) {
            return Status::IOError;
        }

        index_entries_.push_back({cur_last_key_, file_offset_, static_cast<uint32_t>(size)});
        file_offset_ += size;
        cur_block_.clear();
        return Status::OK;
    }

    // 将整个内存表写入 SSTable 文件
    Status SSTableBuilder::Build(const KVStore& store, const std::string& filepath) {
        int fd = open(filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return Status::IOError;

        cur_block_.clear();
        cur_last_key_.clear();
        index_entries_.clear();
        file_offset_ = 0;

        Status result = Status::OK;

        store.ForEach([&](const Key& key, const Value& value) {
            if (result != Status::OK) return;  // 已出错，跳过后续遍历

            std::string entry;
            // 将 KV 编码为 DataBlock 条目格式并放入 entry
            EncodeDataEntry(entry, key, value);

            if (!cur_block_.empty() && cur_block_.size() + entry.size() > kSSTableBlockSize) {
                result = FlushBlock(fd);
                if (result != Status::OK) return;
            }

            cur_block_.append(entry);
            cur_last_key_ = key;
        });

        if (result != Status::OK) {
            close(fd);
            return result;
        }

        // 刷最后一个 Block
        if (!cur_block_.empty()) {
            result = FlushBlock(fd);
            if (result != Status::OK) {
                close(fd);
                return result;
            }
        }

        // 写 IndexBlock
        uint64_t index_off = file_offset_;
        std::string index_buf;
        for (const auto& entry : index_entries_) {
            EncodeIndexEntry(index_buf, entry);
        }
        ssize_t written = write(fd, index_buf.data(), index_buf.size());
        if (written < 0 || static_cast<size_t>(written) != index_buf.size()) {
            close(fd);
            return Status::IOError;
        }

        // 写 Footer
        Footer footer{index_off, static_cast<uint64_t>(index_entries_.size()),
                      kSSTableMagic, kSSTableVersion};
        written = write(fd, &footer, sizeof(footer));
        if (written < 0 || static_cast<size_t>(written) != sizeof(footer)) {
            close(fd);
            return Status::IOError;
        }

        if (fsync(fd) < 0) {
            close(fd);
            return Status::IOError;
        }
        close(fd);
        return Status::OK;
    }

}  // namespace tiny_kv
