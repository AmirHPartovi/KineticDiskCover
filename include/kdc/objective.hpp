#pragma once
#include <stdexcept>
#include <string>
namespace kdc {
enum class ObjectiveType { MIN_MAX, MIN_SUM };
inline const char* to_string(ObjectiveType o) { return o == ObjectiveType::MIN_MAX ? "minmax" : "minsum"; }
inline ObjectiveType objective_from_string(const std::string& s) {
  if (s == "minmax") return ObjectiveType::MIN_MAX;
  if (s == "minsum") return ObjectiveType::MIN_SUM;
  throw std::invalid_argument("unknown objective: " + s);
}
}
