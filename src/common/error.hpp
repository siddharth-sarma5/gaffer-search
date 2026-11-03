#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

// Error-handling policy (docs/DECISIONS.md D-009):
//  - Exceptions derived from gaffer::Error are thrown only for unrecoverable
//    failures at module boundaries: a corpus or index file that can't be read
//    or doesn't parse.
//  - "Not found" is not an error: lookups return std::optional or empty spans.
//  - Hot query paths never throw.
namespace gaffer {

// Base class so callers (e.g. the CLI's main) can catch everything we throw
// with one handler, while still letting std::bad_alloc etc. propagate.
class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// A file or stream could not be opened, read or written.
class IoError : public Error {
 public:
  using Error::Error;
};

// A binary file (e.g. an index file) is malformed, corrupt or from an
// unsupported format version.
class FormatError : public Error {
 public:
  using Error::Error;
};

// Input was readable but malformed. Carries where it went wrong so the
// message points straight at the bad line.
class ParseError : public Error {
 public:
  ParseError(std::string source, std::size_t line, const std::string& detail);

  [[nodiscard]] const std::string& source() const noexcept { return source_; }
  [[nodiscard]] std::size_t line() const noexcept { return line_; }

 private:
  std::string source_;
  std::size_t line_;
};

}  // namespace gaffer
