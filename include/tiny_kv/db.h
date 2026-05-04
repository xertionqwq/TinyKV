#ifndef TINY_KV_DB_H_
#define TINY_KV_DB_H_

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "kv_store.h"
#include "sstable_reader.h"
#include "thread_pool.h"
#include "types.h"
#include "wal.h"

namespace tiny_kv {

	class DB {
		public:
			DB() = default;
			~DB();

			DB(const DB&) = delete;
			DB& operator=(const DB&) = delete;

			Status Open(const std::string& db_path);
			Status Close();

			Status Put(const Key& key, const Value& value);	// 写
			Status Delete(const Key& key);					// 删(本质是仅追加)
			Status Get(const Key& key, Value* value) const;	// 读->>先查 mem_ 和 imm_，再按时间顺序查 sst_readers_

			const std::string& Path() const { return db_path_; }

		private:
			Status RecoverFromWAL();	// 崩溃恢复：回放 WAL 中的记录到 mem_，不修改 SSTable
			Status LoadSSTables();		// 启动时加载 SSTable 文件，构建 sst_readers_
			void   MaybeDumpMemTable();

			std::string db_path_ = "";		//  目录路径，包含 SSTable 文件和 WAL 文件

			std::shared_ptr<KVStore> mem_{nullptr};// 维持当前内存表，所有写操作先更新它
			std::shared_ptr<KVStore> imm_{nullptr};// 正在冻结的内存表，后台线程正在将它转储为 SSTable

			mutable std::shared_mutex mem_mtx_;

			std::vector<std::unique_ptr<SSTableReader>> sst_readers_;
			mutable std::mutex sst_mtx_;

			ThreadPool dump_pool_{1};

			std::unique_ptr<WAL> wal_{nullptr};
			uint64_t next_sst_number_ = 0;
			size_t   mem_approx_bytes_ = 0;
		};

}  // namespace tiny_kv

#endif
