#pragma once
#include "kdc/types.hpp"

#include <string>

namespace kdc {
class DatasetReader {
 public:
  static Instance read_json(const std::string& path);
  static void write_json(const Instance& instance, const std::string& path);
  static Instance read_simple(const std::string& path);
  static Instance generate_random(int n, int m, unsigned seed = 42U,
                                  double T_end = 1.0);
};

bool file_exists(const std::string& path);
}
