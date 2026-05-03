#include "tiny_kv/db.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include "tiny_kv/sstable_builder.h"

namespace tiny_kv {

	DB::~DB() {
		Close();
	}

	Status DB::Open(const std::string& db_path) {
		db_path_ = db_path;

		// 确保目录存在
		mkdir(db_path_.c_str(), 0755);

		// 发现并加载已有 SSTable 文件
		Status s = LoadSSTables();
		if (s != Status::OK) return s;

		// WAL 恢复
		wal_ = std::make_unique<WAL>(db_path_ + "/" + kWALFileName);
		s = wal_->Open();
		if (s != Status::OK) return s;

		s = RecoverFromWAL();
		if (s != Status::OK) return s;

		if (!mem_) {
			mem_ = std::make_shared<KVStore>();
			mem_approx_bytes_ = 0;
		}

		// WAL 回放后 memtable 可能已很大
		if (mem_approx_bytes_ >= kDefaultMemTableSize) {
			MaybeDumpMemTable();
		}

		return Status::OK;
	}

	Status DB::Close() {
		if (wal_) {
			wal_->Sync();
		}

		dump_pool_.Shutdown();

		if (wal_) {
			wal_->Close();
		}

		{
			std::lock_guard<std::mutex> lock(sst_mtx_);
			for (auto& r : sst_readers_) {
				r->Close();
			}
		}

		return Status::OK;
	}

	Status DB::Put(const Key& key, const Value& value) {
		if (key.empty()) return Status::InvalidArgument;

		wal_->PutRecord(key, value);

		{
			std::unique_lock<std::shared_mutex> lock(mem_mtx_);
			mem_->Put(key, value);
			mem_approx_bytes_ += key.size() + value.size();
		}

		if (mem_approx_bytes_ >= kDefaultMemTableSize) {
			MaybeDumpMemTable();
		}

		return Status::OK;
	}

	Status DB::Delete(const Key& key) {
		if (key.empty()) return Status::InvalidArgument;

		wal_->DeleteRecord(key);

		{
			std::unique_lock<std::shared_mutex> lock(mem_mtx_);
			mem_->Put(key, kTombstoneValue);
			mem_approx_bytes_ += key.size() + kTombstoneValue.size();
		}

		return Status::OK;
	}

	Status DB::Get(const Key& key, Value* value) const {
		if (key.empty()) return Status::InvalidArgument;

		// 1. 查当前 memtable
		{
			std::shared_lock<std::shared_mutex> lock(mem_mtx_);
			if (mem_ && mem_->Get(key, value)) {
				if (*value == kTombstoneValue) return Status::NotFound;
				return Status::OK;
			}
			// 2. 查 immutable memtable
			if (imm_ && imm_->Get(key, value)) {
				if (*value == kTombstoneValue) return Status::NotFound;
				return Status::OK;
			}
		}

		// 3. 查 SSTable（新的在前）
		{
			std::lock_guard<std::mutex> lock(sst_mtx_);
			for (auto& r : sst_readers_) {
				Status s = r->Get(key, value);
				if (s == Status::OK) {
					if (*value == kTombstoneValue) return Status::NotFound;
					return Status::OK;
				}
			}
		}

		return Status::NotFound;
	}

	Status DB::RecoverFromWAL() {
		mem_ = std::make_shared<KVStore>();
		mem_approx_bytes_ = 0;

		return wal_->Replay([this](const Key& key, const Value& value, RecordType type) {
			if (type == RecordType::Delete) {
				mem_->Put(key, kTombstoneValue);
				mem_approx_bytes_ += key.size() + kTombstoneValue.size();
			} else {
				mem_->Put(key, value);
				mem_approx_bytes_ += key.size() + value.size();
			}
		});
	}

	Status DB::LoadSSTables() {
		// 扫描 db_path 下匹配 NNNNNN.sst 的文件
		DIR* dir = opendir(db_path_.c_str());
		if (!dir) return Status::IOError;

		std::vector<uint64_t> numbers;

		struct dirent* entry;
		while ((entry = readdir(dir)) != nullptr) {
			const char* name = entry->d_name;
			size_t len = std::strlen(name);
			if (len != 10) continue;  // "NNNNNN.sst" = 6 + 4
			if (std::strcmp(name + len - 4, ".sst") != 0) continue;

			uint64_t num = 0;
			bool valid = true;
			for (int i = 0; i < 6; ++i) {
				if (name[i] < '0' || name[i] > '9') { valid = false; break; }
				num = num * 10 + static_cast<uint64_t>(name[i] - '0');
			}
			if (!valid) continue;

			numbers.push_back(num);
		}
		closedir(dir);

		// 按序号升序（旧的在前），Open 后翻转
		std::sort(numbers.begin(), numbers.end());

		for (auto num : numbers) {
			auto reader = std::make_unique<SSTableReader>();
			Status s = reader->Open(SSTableFileName(db_path_, num));
			if (s != Status::OK) return s;
			sst_readers_.push_back(std::move(reader));
		}

		// 翻转：新的在前
		std::reverse(sst_readers_.begin(), sst_readers_.end());

		if (!numbers.empty()) {
			next_sst_number_ = numbers.back() + 1;
		}

		return Status::OK;
	}

	void DB::MaybeDumpMemTable() {
		std::shared_ptr<KVStore> to_dump;
		{
			std::unique_lock<std::shared_mutex> lock(mem_mtx_);
			if (mem_approx_bytes_ < kDefaultMemTableSize) return;
			if (imm_) return;

			to_dump = mem_;
			imm_ = mem_;
			mem_ = std::make_shared<KVStore>();
			mem_approx_bytes_ = 0;
		}

		uint64_t sst_num = next_sst_number_++;
		std::string sst_path = SSTableFileName(db_path_, sst_num);

		dump_pool_.Submit([this, to_dump, sst_path] {
			SSTableBuilder().Build(*to_dump, sst_path);

			auto reader = std::make_unique<SSTableReader>();
			Status s = reader->Open(sst_path);
			if (s == Status::OK) {
				std::lock_guard<std::mutex> lock(sst_mtx_);
				sst_readers_.insert(sst_readers_.begin(), std::move(reader));
			}

			std::unique_lock<std::shared_mutex> lock(mem_mtx_);
			imm_.reset();
		});
	}

}  // namespace tiny_kv
