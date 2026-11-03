#include "common/error.hpp"

#include <string>
#include <utility>

namespace gaffer {

namespace {

std::string format_parse_error(const std::string& source, std::size_t line,
                               const std::string& detail) {
  return source + ":" + std::to_string(line) + ": " + detail;
}

}  // namespace

// Format first, from the still-valid parameters, then move them into members.
ParseError::ParseError(std::string source, std::size_t line, const std::string& detail)
    : Error(format_parse_error(source, line, detail)), source_(std::move(source)), line_(line) {}

}  // namespace gaffer
