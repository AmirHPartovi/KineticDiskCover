#pragma once
#include <stdexcept>
#include <string>
namespace kdc {
enum class ObjectiveType { MIN_MAX, MIN_SUM, MIN_MAX_SUM };
inline const char* to_string(ObjectiveType o) {
  switch (o) {
    case ObjectiveType::MIN_MAX:
      return "minmax";
    case ObjectiveType::MIN_SUM:
      return "minsum";
    case ObjectiveType::MIN_MAX_SUM:
      return "minmaxsum";
  }
  throw std::invalid_argument("unknown objective");
}
inline ObjectiveType objective_from_string(const std::string& s) {
  if (s == "minmax") return ObjectiveType::MIN_MAX;
  if (s == "minsum") return ObjectiveType::MIN_SUM;
  if (s == "minmaxsum") return ObjectiveType::MIN_MAX_SUM;
  throw std::invalid_argument("unknown objective: " + s);
}
}
