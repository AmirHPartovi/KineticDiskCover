#include "kdc/trace.hpp"

#include "kdc/logging.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace kdc {
namespace {
constexpr const char* kCsvHeader =
    "iter,t_max,objective,lower_bound,gap,wall_time,num_ip_solves";

std::vector<std::string> split_fields(const std::string& line) {
  std::vector<std::string> fields;
  std::istringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ',')) {
    fields.push_back(field);
  }
  if (!line.empty() && line.back() == ',') {
    fields.emplace_back();
  }
  return fields;
}

template <typename T>
T parse_field(const std::string& text, const char* field_name) {
  std::istringstream stream(text);
  T value{};
  stream >> value;
  if (!stream || stream.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error(std::string("invalid trace CSV ") + field_name);
  }
  return value;
}

bool valid_row(const IterTrace& row) {
  return row.iter >= 0 && std::isfinite(row.t_max) &&
         std::isfinite(row.objective_value) &&
         std::isfinite(row.lower_bound) && std::isfinite(row.gap) &&
         std::isfinite(row.wall_time_sec) && row.wall_time_sec >= 0.0 &&
         row.num_ip_solves >= 0;
}
}  // namespace

void TraceWriter::write_csv(const std::vector<IterTrace>& trace,
                            const std::string& path) {
  LOG_DEBUG("TraceWriter::write_csv rows={} path={}", trace.size(), path);
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open trace CSV for writing: " + path);
  }
  output << kCsvHeader << '\n'
         << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto& row : trace) {
    if (!valid_row(row)) {
      throw std::invalid_argument("trace contains an invalid row");
    }
    output << row.iter << ',' << row.t_max << ',' << row.objective_value << ','
           << row.lower_bound << ',' << row.gap << ',' << row.wall_time_sec
           << ',' << row.num_ip_solves << '\n';
  }
  if (!output) {
    throw std::runtime_error("failed writing trace CSV: " + path);
  }
  LOG_INFO("TraceWriter: wrote {} rows to {}", trace.size(), path);
}

std::vector<IterTrace> TraceWriter::read_csv(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open trace CSV for reading: " + path);
  }
  std::string line;
  if (!std::getline(input, line) || line != kCsvHeader) {
    throw std::runtime_error("invalid trace CSV header");
  }

  std::vector<IterTrace> trace;
  std::size_t line_number = 1U;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      throw std::runtime_error("empty trace CSV row at line " +
                               std::to_string(line_number));
    }
    const auto fields = split_fields(line);
    if (fields.size() != 7U) {
      throw std::runtime_error("trace CSV row must have 7 fields at line " +
                               std::to_string(line_number));
    }
    try {
      IterTrace row;
      row.iter = parse_field<int>(fields[0], "iteration");
      row.t_max = parse_field<double>(fields[1], "t_max");
      row.objective_value = parse_field<double>(fields[2], "objective");
      row.lower_bound = parse_field<double>(fields[3], "lower_bound");
      row.gap = parse_field<double>(fields[4], "gap");
      row.wall_time_sec = parse_field<double>(fields[5], "wall_time");
      row.num_ip_solves = parse_field<int>(fields[6], "num_ip_solves");
      if (!valid_row(row)) {
        throw std::runtime_error("trace row contains invalid values");
      }
      trace.push_back(row);
    } catch (const std::exception& error) {
      throw std::runtime_error("invalid trace CSV at line " +
                               std::to_string(line_number) + ": " +
                               error.what());
    }
  }
  if (input.bad()) {
    throw std::runtime_error("failed reading trace CSV: " + path);
  }
  return trace;
}

void TraceWriter::log_summary(const std::vector<IterTrace>& trace) {
  if (trace.empty()) {
    LOG_INFO("TraceWriter: no iterations recorded");
    return;
  }
  LOG_INFO("TraceWriter: {} iterations, first gap={:.6g}, final gap={:.6g}",
           trace.size(), trace.front().gap, trace.back().gap);
  for (std::size_t index = 0; index < trace.size(); ++index) {
    if (index == 0U || index + 1U == trace.size() || index % 10U == 0U) {
      const auto& row = trace[index];
      LOG_INFO("  iter={} t_max={:.6g} objective={:.9g} LB={:.9g} gap={:.6g}",
               row.iter, row.t_max, row.objective_value, row.lower_bound,
               row.gap);
    }
  }
}
}  // namespace kdc
