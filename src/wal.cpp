#include "tiny_kv/wal.h"

#include <fcntl.h>
#include <unistd.h>

namespace tiny_kv {

	WAL::WAL(const std::string& filepath) : filepath_(filepath) {}

	WAL::~WAL() {
		Close();
	}

	Status WAL::Open() {
		// 只写 + 追加打卡fd
		fd_ = open(filepath_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd_ < 0) return Status::IOError;

		stop_ = false;
		write_error_ = false;
		// 开辟一个后台写线程, 作消费者
		writer_thread_ = std::thread([this] { WriterThread(); });
		return Status::OK;
	}

	Status WAL::Close() {
		if (fd_ < 0) return Status::OK;

		{
			std::lock_guard<std::mutex> lock(mtx_);
			stop_ = true;
		}
		cv_.notify_all();	// 通知线程完成任务

		if (writer_thread_.joinable()) {
			writer_thread_.join();
		}

		Status result = Status::OK;
		if (write_error_) result = Status::IOError;

		// fsync() 强制内核立刻把缓冲区里属于这个文件的数据刷到磁盘硬件上。fsync 
		// 返回之前，数据已经写在物理盘片上。断电重启后数据还在
		if (fsync(fd_) < 0) result = Status::IOError;
		if (close(fd_) < 0) result = Status::IOError;
		fd_ = -1;
		return result;
	}

	Status WAL::PutRecord(const Key& key, const Value& value) {
		if (fd_ < 0) return Status::IOError;

		// 序列化kv, 以便落盘
		std::string buf = SerializeRecord(RecordType::Put, key, value);
		{
			std::lock_guard<std::mutex> lock(mtx_);
			write_queue_.push(std::move(buf));
		}
		cv_.notify_one(); // 通知一线程来取任务, put不阻塞
		return Status::OK;
	}

	Status WAL::DeleteRecord(const Key& key) {
		if (fd_ < 0) return Status::IOError;

		Value empty;
		std::string buf = SerializeRecord(RecordType::Delete, key, empty);
		{
			std::lock_guard<std::mutex> lock(mtx_);
			write_queue_.push(std::move(buf));
		}
		cv_.notify_one();
		return Status::OK;
	}

	// 强制清空任务队列, 实现所有buf_落盘
	Status WAL::Sync() {
		if (fd_ < 0) return Status::IOError;

		{
			std::unique_lock<std::mutex> lock(mtx_);
			cv_.wait(lock, [this] {
				return write_queue_.empty() && pending_writes_ == 0;
			});
		}

		if (write_error_) return Status::IOError;
		if (fsync(fd_) < 0) return Status::IOError;
		return Status::OK;
	}

	// 记录格式: [Type(1B)|KeyLen(4B)|ValueLen(4B)|Key|Value]
	// 将 type key value 序列化成一个连续的字符串，方便写入磁盘
	std::string WAL::SerializeRecord(RecordType type, const Key& key, const Value& value) {
		uint8_t  ty      = static_cast<uint8_t>(type);
		uint32_t key_len = static_cast<uint32_t>(key.size());
		uint32_t val_len = static_cast<uint32_t>(value.size());

		std::string buf;
		buf.reserve(sizeof(ty) + sizeof(key_len) + sizeof(val_len) + key.size() + value.size());
		// append-only
		buf.append(reinterpret_cast<const char*>(&ty), sizeof(ty));
		buf.append(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
		buf.append(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
		buf.append(key);
		buf.append(value);
		return buf;
	}

	// write 线程, 将buf_交给内核
	void WAL::WriterThread() {
		while (true) {
			std::unique_lock<std::mutex> lock(mtx_);
			cv_.wait(lock, [this] { return !write_queue_.empty() || stop_; });

			if (stop_ && write_queue_.empty()) return;

			std::string buf = std::move(write_queue_.front());
			write_queue_.pop();
			pending_writes_++;
			lock.unlock();

			// 三个参数：文件描述符 + 数据指针 + 字节数。返回值是实际写入的字节数
			ssize_t n = write(fd_, buf.data(), buf.size());

			lock.lock();
			pending_writes_--;
			// write 可能返回 -1（错误）或小于 buf.size()（部分写入），都视为错误
			if (n < 0 || static_cast<size_t>(n) != buf.size()) write_error_ = true;
			if (pending_writes_ == 0) cv_.notify_all();
			lock.unlock();
		}
	}

	// 崩溃恢复
	Status WAL::Replay(const ReplayCallback& callback) {
		int replay_fd = open(filepath_.c_str(), O_RDONLY);
		if (replay_fd < 0) {
			return Status::IOError;
		}

		Status result = Status::OK;
		while (true) {
			uint8_t  ty = 0;
			uint32_t key_len = 0;
			uint32_t val_len = 0;

			// 读取头部
			ssize_t n = read(replay_fd, &ty, sizeof(ty));
			if (n == 0) break;  // EOF
			// 分别读一字节和 kv 字节数, 如果读失败或读到的字节数不对，说明文件损坏了
			if (n < 0 || static_cast<size_t>(n) != sizeof(ty)) { result = Status::Corruption; break; }

			n = read(replay_fd, &key_len, sizeof(key_len));
			if (n < 0 || static_cast<size_t>(n) != sizeof(key_len)) { result = Status::Corruption; break; }

			n = read(replay_fd, &val_len, sizeof(val_len));
			if (n < 0 || static_cast<size_t>(n) != sizeof(val_len)) { result = Status::Corruption; break; }

			// 读取 Key
			Key key(key_len, '\0');
			n = read(replay_fd, key.data(), key_len);
			if (n < 0 || static_cast<size_t>(n) != key_len) { result = Status::Corruption; break; }

			// 读取 Value
			Value value(val_len, '\0');
			if (val_len > 0) {
				n = read(replay_fd, value.data(), val_len);
				if (n < 0 || static_cast<size_t>(n) != val_len) { result = Status::Corruption; break; }
			}

			callback(key, value, static_cast<RecordType>(ty));
		}

		close(replay_fd);
		return result;
	}

}  // namespace tiny_kv
