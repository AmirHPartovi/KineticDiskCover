#include "kdc/cli_utils.hpp"

#include "kdc/logging.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace kdc {
CliArgs CliArgs::parse(int argc, char** argv) {
  CliArgs result;
  for (int index = 1; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (argument.compare(0, 2, "--") != 0) {
      result.positional_.push_back(argument);
      continue;
    }

    const std::string flag_value = argument.substr(2);
    const std::string::size_type separator = flag_value.find('=');
    std::string key = flag_value.substr(0, separator);
    if (key.empty()) {
      throw std::invalid_argument("empty command-line flag");
    }
    std::string value;
    if (separator != std::string::npos) {
      value = flag_value.substr(separator + 1U);
    } else if (index + 1 < argc && argv[index + 1][0] != '-') {
      value = argv[++index];
    }
    result.flags_[std::move(key)] = std::move(value);
  }
  return result;
}

bool CliArgs::has(const std::string& flag) const {
  return flags_.find(flag) != flags_.end();
}

std::string CliArgs::get(const std::string& flag,
                         const std::string& default_value) const {
  const auto found = flags_.find(flag);
  return found == flags_.end() ? default_value : found->second;
}

int CliArgs::get_int(const std::string& flag, int default_value) const {
  const auto found = flags_.find(flag);
  if (found == flags_.end()) {
    return default_value;
  }
  try {
    std::size_t parsed = 0;
    const int value = std::stoi(found->second, &parsed);
    if (parsed != found->second.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return value;
  } catch (const std::exception& error) {
    LOG_WARN("invalid integer for --{} ('{}'): {}; using default {}", flag,
             found->second, error.what(), default_value);
    return default_value;
  }
}

double CliArgs::get_double(const std::string& flag,
                           double default_value) const {
  const auto found = flags_.find(flag);
  if (found == flags_.end()) {
    return default_value;
  }
  try {
    std::size_t parsed = 0;
    const double value = std::stod(found->second, &parsed);
    if (parsed != found->second.size() || !std::isfinite(value)) {
      throw std::invalid_argument("value is not a finite number");
    }
    return value;
  } catch (const std::exception& error) {
    LOG_WARN("invalid number for --{} ('{}'): {}; using default {}", flag,
             found->second, error.what(), default_value);
    return default_value;
  }
}

const std::vector<std::string>& CliArgs::positional() const {
  return positional_;
}

void print_usage(const std::string& subcommand) {
  if (subcommand == "solve") {
    std::cout << "Usage: kdc-solver solve [OPTIONS]\n"
                 "  --instance FILE          Path to instance JSON (required)\n"
                 "  --mode minmax|minsum     Objective (default: minmax)\n"
                 "  --algorithm NAME         Static solver (default: ip-kont)\n"
                 "  --output FILE            Write KineticSolution as JSON to FILE\n"
                 "  --time-limit SEC         Per-IP time limit (default: 600)\n"
                 "  --fast-time-limit SEC    Global heuristic deadline (default: 30)\n"
                 "  --exact-time-limit SEC   Global exact deadline (default: 600)\n"
                 "  --gap TARGET             Target optimality gap (default: 0.01)\n"
                 "  --minsum-refinement-policy adaptive|sampled "
                 "(default: adaptive)\n"
                 "  --verify-each-iteration  Verify every accepted iteration\n"
                 "  --no-verify              Skip post-solve verification\n"
                 "  --no-handovers           Disable handover-based extension\n"
                 "  --no-dedup               Disable duplicate-interval removal\n"
                 "  --no-partial             Disable partial extension\n"
                 "  --help                   Show this message\n";
    return;
  }
  if (subcommand == "verify") {
    std::cout << "Usage: kdc-solver verify --instance FILE --solution FILE\n";
  } else if (subcommand == "benchmark") {
    std::cout << "Usage: kdc-solver benchmark --dataset DIR --output DIR "
                 "--mode both|minmax|minsum\n";
  } else if (subcommand == "compare") {
    std::cout << "Usage: kdc-solver compare --algorithms a,b --dataset DIR "
                 "--output DIR [--mode static|minmax|minsum]\n";
  } else {
    std::cout << "Usage: kdc-solver <command> [options]\n";
  }
}
}  // namespace kdc
