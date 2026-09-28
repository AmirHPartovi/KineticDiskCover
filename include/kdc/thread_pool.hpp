#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace kdc {
class ThreadPool {
 public:
  explicit ThreadPool(int num_threads);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <typename F, typename... Args>
  auto submit(F&& function, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>> {
    using Result = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<Result()>>(
        [callable = std::forward<F>(function),
         arguments = std::make_tuple(std::forward<Args>(args)...)]() mutable {
          return std::apply(std::move(callable), std::move(arguments));
        });
    std::future<Result> future = task->get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        throw std::runtime_error("ThreadPool is stopping");
      }
      tasks_.emplace([task]() { (*task)(); });
      ++active_tasks_;
    }
    task_ready_.notify_one();
    return future;
  }

  int size() const { return static_cast<int>(workers_.size()); }
  void wait_idle();
  void shutdown();

 private:
  void worker_loop();

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  mutable std::mutex mutex_;
  std::condition_variable task_ready_;
  std::condition_variable idle_;
  int active_tasks_{0};
  bool stopping_{false};
};
}  // namespace kdc
