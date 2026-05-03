#ifndef TINY_KV_THREAD_POOL_H_
#define TINY_KV_THREAD_POOL_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tiny_kv {

	class ThreadPool {
		public:
			// 传 0 或不传取 std::thread::hardware_concurrency()，至少为 1
			explicit ThreadPool(int num_threads = 0);
			~ThreadPool();

			ThreadPool(const ThreadPool&) = delete;
			ThreadPool& operator=(const ThreadPool&) = delete;
			ThreadPool(ThreadPool&&) = delete;
			ThreadPool& operator=(ThreadPool&&) = delete;

			// 提交任务。Shutdown 后不应再调用
			void Submit(std::function<void()> task);

			// 排空队列、通知线程退出、join 等待。可重复调用
			void Shutdown();

		private:
			void WorkerLoop();

			std::vector<std::thread> workers_;
			std::queue<std::function<void()>> tasks_;
			std::mutex mtx_;
			std::condition_variable cv_;
			bool stop_ = false;
		};

}  // namespace tiny_kv

#endif
