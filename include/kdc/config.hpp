#pragma once
#include "kdc/objective.hpp"
#include <string>
namespace kdc {
struct GlobalConfig {
  ObjectiveType objective{ObjectiveType::MIN_MAX};
  std::string dataset_dir{"data"};
  std::string output_dir{"results"};
  std::string log_level{"info"};
  std::string log_file{"results/logs/kdc.log"};
  int num_threads{1};
  bool verify_after_solve{true};
  std::string kont_root{""};
};
}
