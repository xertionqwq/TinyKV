#include "tiny_kv/wal.h"

#include <fcntl.h>
#include <unistd.h>

namespace tiny_kv {

	WAL::WAL(const std::string& filepath) : filepath_(filepath) {}

	WAL::~WAL() {
		Close();
	}

	Status WAL::Open() {
		fd_ = open(filepath_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd_ < 0) return Status::IOError;

		stop_ = false;
		write_error_ = false;
		writer_thread_ = std::thread([this] { WriterThread(); });
		return Status::OK;
	}

	Status WAL::Close() {
		if (fd_ < 0) return Status::OK;

		{
			std::lock_guard<std::mutex> lock(mtx_);
			stop_ = true;
		}
		cv_.notify_all();

		if (writer_thread_.joinable()) {
			writer_thread_.join();
		}

		Status result = Status::OK;
		if (write_error_) result = Status::IOError;

		if (fsync(fd_) < 0) result = Status::IOError;
		if (close(fd_) < 0) result = Status::IOError;
		fd_ = -1;
		return result;
	}

	Status WAL::PutRecord(const Key& key, const Value& value) {
		if (fd_ < 0) return Status::IOError;

		std::string buf = SerializeRecord(RecordType::Put, key, value);
		{
			std::lock_guard<std::mutex> lock(mtx_);
			write_queue_.push(std::move(buf));
		}
		cv_.notify_one();
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

	std::string WAL::SerializeRecord(RecordType type, const Key& key, const Value& value) {
		uint8_t  ty      = static_cast<uint8_t>(type);
		uint32_t key_len = static_cast<uint32_t>(key.size());
		uint32_t val_len = static_cast<uint32_t>(value.size());

		std::string buf;
		buf.reserve(sizeof(ty) + sizeof(key_len) + sizeof(val_len) + key.size() + value.size());
		buf.append(reinterpret_cast<const char*>(&ty), sizeof(ty));
		buf.append(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
		buf.append(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
		buf.append(key);
		buf.append(value);
		return buf;
	}

	void WAL::WriterThread() {
		while (true) {
			std::unique_lock<std::mutex> lock(mtx_);
			cv_.wait(lock, [this] { return !write_queue_.empty() || stop_; });

			if (stop_ && write_queue_.empty()) return;

			std::string buf = std::move(write_queue_.front());
			write_queue_.pop();
			pending_writes_++;
			lock.unlock();

			ssize_t n = write(fd_, buf.data(), buf.size());

			lock.lock();
			pending_writes_--;
			if (n < 0 || static_cast<size_t>(n) != buf.size()) write_error_ = true;
			if (pending_writes_ == 0) cv_.notify_all();
			lock.unlock();
		}
	}

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
