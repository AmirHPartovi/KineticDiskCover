#pragma once

#include "kdc/solution.hpp"
#include "kdc/types.hpp"

#include <string>

namespace kdc {
class SolutionSerializer {
 public:
  static void save_json(const Instance& instance, const KineticSolution& solution,
                        const std::string& path);

  static KineticSolution load_json(const Instance& instance,
                                   const std::string& path);

  static bool validate(const Instance& instance,
                       const KineticSolution& solution,
                       std::string* out_error = nullptr);
};
}  // namespace kdc
