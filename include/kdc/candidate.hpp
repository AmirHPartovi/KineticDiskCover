#pragma once

#include "kdc/types.hpp"

#include <vector>

namespace kdc {
struct CandidateDisk {
  int station_id{-1};
  int supporting_point{-1};
};

class CandidateSet {
 public:
  static std::vector<CandidateDisk> build(const Instance& instance);

  struct CoverageMatrix {
    int num_disks{0};
    int num_points{0};
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
  };

  static CoverageMatrix build_coverage(
      const Instance& instance, const std::vector<CandidateDisk>& disks,
      double time = 0.0);

  static void dump(const std::vector<CandidateDisk>& disks);
};
}
