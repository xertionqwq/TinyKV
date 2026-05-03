#include "tiny_kv/thread_pool.h"

namespace tiny_kv {

	ThreadPool::ThreadPool(int num_threads) {
		if (num_threads <= 0) {
			num_threads = static_cast<int>(std::thread::hardware_concurrency());
			if (num_threads <= 0) num_threads = 1;
		}

		workers_.reserve(static_cast<size_t>(num_threads));
		for (int i = 0; i < num_threads; ++i) {
			workers_.emplace_back([this] { WorkerLoop(); });
		}
	}

	ThreadPool::~ThreadPool() {
		Shutdown();
	}

	void ThreadPool::Submit(std::function<void()> task) {
		{
			std::lock_guard<std::mutex> lock(mtx_);
			tasks_.push(std::move(task));
		}
		cv_.notify_one();
	}

	void ThreadPool::Shutdown() {
		if (stop_) return;

		{
			std::lock_guard<std::mutex> lock(mtx_);
			stop_ = true;
		}
		cv_.notify_all();

		for (auto& t : workers_) {
			if (t.joinable()) t.join();
		}
	}

	void ThreadPool::WorkerLoop() {
		while (true) {
			std::unique_lock<std::mutex> lock(mtx_);
			cv_.wait(lock, [this] { return !tasks_.empty() || stop_; });

			if (stop_ && tasks_.empty()) return;

			auto task = std::move(tasks_.front());
			tasks_.pop();
			lock.unlock();

			try {
				task();
			} catch (...) {
				// 吞掉异常，不拖垮工作线程
			}
		}
	}

}  // namespace tiny_kv
