#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace kdc {
class CliArgs {
 public:
  static CliArgs parse(int argc, char** argv);

  bool has(const std::string& flag) const;
  std::string get(const std::string& flag,
                  const std::string& default_value = "") const;
  int get_int(const std::string& flag, int default_value) const;
  double get_double(const std::string& flag, double default_value) const;
  const std::vector<std::string>& positional() const;

 private:
  std::unordered_map<std::string, std::string> flags_;
  std::vector<std::string> positional_;
};

void print_usage(const std::string& subcommand);
}  // namespace kdc
