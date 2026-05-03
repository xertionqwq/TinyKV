#include "tiny_kv/sstable_reader.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>

namespace tiny_kv {

    SSTableReader::~SSTableReader() {
        if (fd_ >= 0) close(fd_);
    }

    Status SSTableReader::Open(const std::string& filepath) {
        filepath_ = filepath;
        fd_ = open(filepath.c_str(), O_RDONLY);
        if (fd_ < 0) return Status::IOError;

        // 获取文件大小
        off_t file_size = lseek(fd_, 0, SEEK_END);
        if (file_size < 0) {
            close(fd_);
            fd_ = -1;
            return Status::IOError;
        }
        if (static_cast<size_t>(file_size) < sizeof(Footer)) {
            close(fd_);
            fd_ = -1;
            return Status::Corruption;
        }

        // 读 Footer（文件末尾 24 字节）
        Footer footer;
        ssize_t n = pread(fd_, &footer, sizeof(footer), file_size - sizeof(footer));
        if (n < 0 || static_cast<size_t>(n) != sizeof(footer)) {
            close(fd_);
            fd_ = -1;
            return Status::IOError;
        }

        if (footer.magic != kSSTableMagic || footer.version != kSSTableVersion) {
            close(fd_);
            fd_ = -1;
            return Status::Corruption;
        }

        // 读 IndexBlock
        size_t index_size = static_cast<size_t>(file_size) - sizeof(Footer) - footer.index_offset;
        std::string index_buf(index_size, '\0');
        n = pread(fd_, index_buf.data(), index_size, static_cast<off_t>(footer.index_offset));
        if (n < 0 || static_cast<size_t>(n) != index_size) {
            close(fd_);
            fd_ = -1;
            return Status::IOError;
        }

        // 解析 IndexEntry
        index_entries_.clear();
        index_entries_.reserve(footer.index_count);

        size_t off = 0;
        for (uint64_t i = 0; i < footer.index_count; ++i) {
            IndexEntry entry;
            size_t n_consumed = 0;
            if (!DecodeIndexEntry(index_buf.data() + off, index_size - off,
                                  &n_consumed, &entry)) {
                close(fd_);
                fd_ = -1;
                return Status::Corruption;
            }
            off += n_consumed;
            index_entries_.push_back(std::move(entry));
        }

        return Status::OK;
    }

    Status SSTableReader::Get(const Key& target, Value* value) const {
        if (index_entries_.empty()) return Status::NotFound;

        // 二分 IndexBlock，找第一个 last_key >= target 的条目
        auto it = std::lower_bound(
            index_entries_.begin(), index_entries_.end(), target,
            [](const IndexEntry& e, const Key& k) { return e.last_key < k; });

        if (it == index_entries_.end()) return Status::NotFound;

        // 读 DataBlock
        std::string block_buf(it->size, '\0');
        ssize_t n = pread(fd_, block_buf.data(), it->size, static_cast<off_t>(it->offset));
        if (n < 0 || static_cast<uint32_t>(n) != it->size) return Status::IOError;

        // 线性扫描 Block
        size_t off = 0;
        Key    key;
        while (off < it->size) {
            size_t n_consumed = 0;
            if (!DecodeDataEntry(block_buf.data() + off, it->size - off,
                                 &n_consumed, &key, value)) {
                return Status::Corruption;
            }
            if (key == target) return Status::OK;
            if (key > target) break;  // 有序，后面不可能比 target 小
            off += n_consumed;
        }

        return Status::NotFound;
    }

    Status SSTableReader::Close() {
        if (fd_ >= 0) {
            if (close(fd_) < 0) return Status::IOError;
            fd_ = -1;
        }
        return Status::OK;
    }

}  // namespace tiny_kv
