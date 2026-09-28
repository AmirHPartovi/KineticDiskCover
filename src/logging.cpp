#include "kdc/logging.hpp"
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace kdc {
void init_logging(const std::string& level, const std::string& log_file) {
  const auto log_level = spdlog::level::from_str(level);
  auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console->set_level(log_level);
  std::vector<spdlog::sink_ptr> sinks{console};

  try {
    std::filesystem::path path(log_file);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_file, 10U * 1024U * 1024U, 3U);
    file->set_level(log_level);
    sinks.push_back(file);
  } catch (const std::filesystem::filesystem_error& ex) {
    std::cerr << "kdc logging file sink unavailable: " << ex.what() << '\n';
  } catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "kdc logging file sink unavailable: " << ex.what() << '\n';
  }

  try {
    spdlog::init_thread_pool(8192U, 1U);
    auto logger = std::make_shared<spdlog::async_logger>(
        "kdc", sinks.begin(), sinks.end(), spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);
    logger->set_level(log_level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(log_level);
  } catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "kdc asynchronous logging unavailable: " << ex.what() << '\n';
    auto logger = std::make_shared<spdlog::logger>("kdc", sinks.begin(), sinks.end());
    logger->set_level(log_level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(log_level);
  }
}
}
