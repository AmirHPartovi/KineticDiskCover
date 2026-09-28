#include "kdc/thread_pool.hpp"

#include <algorithm>
#include <stdexcept>

namespace kdc {
ThreadPool::ThreadPool(int num_threads) {
  if (num_threads <= 0) {
    num_threads = static_cast<int>(
        std::max(1U, std::thread::hardware_concurrency()));
  }
  workers_.reserve(static_cast<std::size_t>(num_threads));
  try {
    for (int index = 0; index < num_threads; ++index) {
      workers_.emplace_back([this]() { worker_loop(); });
    }
  } catch (...) {
    shutdown();
    throw;
  }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::worker_loop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      task_ready_.wait(lock,
                       [this]() { return stopping_ || !tasks_.empty(); });
      if (stopping_ && tasks_.empty()) {
        return;
      }
      task = std::move(tasks_.front());
      tasks_.pop();
    }
    task();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      --active_tasks_;
      if (active_tasks_ == 0) {
        idle_.notify_all();
      }
    }
  }
}

void ThreadPool::wait_idle() {
  std::unique_lock<std::mutex> lock(mutex_);
  idle_.wait(lock, [this]() { return active_tasks_ == 0 && tasks_.empty(); });
}

void ThreadPool::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  task_ready_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}
}  // namespace kdc
