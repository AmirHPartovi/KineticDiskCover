#include "kdc/thread_pool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

TEST_CASE("ThreadPool: executes a single task") {
  kdc::ThreadPool pool(1);
  auto future = pool.submit([]() { return 42; });
  REQUIRE(future.get() == 42);
}

TEST_CASE("ThreadPool: executes many tasks") {
  kdc::ThreadPool pool(4);
  std::vector<std::future<int>> futures;
  for (int value = 0; value < 100; ++value) {
    futures.push_back(pool.submit([value]() { return value * value; }));
  }
  for (int value = 0; value < 100; ++value) {
    REQUIRE(futures[static_cast<std::size_t>(value)].get() == value * value);
  }
}

TEST_CASE("ThreadPool: wait_idle waits for all submitted work") {
  kdc::ThreadPool pool(2);
  std::atomic<int> completed{0};
  for (int task = 0; task < 10; ++task) {
    pool.submit([&completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      completed.fetch_add(1);
    });
  }
  pool.wait_idle();
  REQUIRE(completed.load() == 10);
}

TEST_CASE("ThreadPool: propagates task exceptions through futures") {
  kdc::ThreadPool pool(1);
  auto future = pool.submit(
      []() -> int { throw std::runtime_error("thread-pool task failure"); });
  REQUIRE_THROWS(future.get());
}

TEST_CASE("ThreadPool: shutdown drains queued tasks") {
  kdc::ThreadPool pool(2);
  std::atomic<int> completed{0};
  for (int task = 0; task < 20; ++task) {
    pool.submit([&completed]() { completed.fetch_add(1); });
  }
  pool.shutdown();
  REQUIRE(completed.load() == 20);
  REQUIRE_THROWS_AS(pool.submit([]() {}), std::runtime_error);
}

TEST_CASE("ThreadPool: zero threads selects at least one worker") {
  kdc::ThreadPool pool(0);
  REQUIRE(pool.size() >= 1);
}
