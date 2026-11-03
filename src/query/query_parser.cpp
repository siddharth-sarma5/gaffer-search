#include "query/query_parser.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gaffer::query {

namespace {

constexpr std::string_view kLeftDoubleQuote = "“";   // “ (3 bytes in UTF-8)
constexpr std::string_view kRightDoubleQuote = "”";  // ”

// Length in bytes of a quote character starting at query[i], or 0 if none.
std::size_t quote_at(std::string_view query, std::size_t i) noexcept {
  const std::string_view rest = query.substr(i);
  if (rest.starts_with('"')) {
    return 1;
  }
  if (rest.starts_with(kLeftDoubleQuote) || rest.starts_with(kRightDoubleQuote)) {
    return kLeftDoubleQuote.size();
  }
  return 0;
}

bool is_blank(std::string_view s) noexcept {
  return s.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

void add_clause(std::vector<Clause>& out, ClauseKind kind, std::string_view text) {
  if (!is_blank(text)) {
    out.push_back(Clause{kind, std::string(text)});
  }
}

}  // namespace

std::vector<Clause> parse_query(std::string_view query) {
  std::vector<Clause> clauses;
  bool in_phrase = false;
  std::size_t start = 0;  // start of the current clause's text
  std::size_t i = 0;
  while (i < query.size()) {
    const std::size_t quote_len = quote_at(query, i);
    if (quote_len == 0) {
      ++i;
      continue;
    }
    add_clause(clauses, in_phrase ? ClauseKind::kPhrase : ClauseKind::kTerms,
               query.substr(start, i - start));
    in_phrase = !in_phrase;
    i += quote_len;
    start = i;
  }
  // Whatever is left; an open quote makes it a phrase.
  add_clause(clauses, in_phrase ? ClauseKind::kPhrase : ClauseKind::kTerms, query.substr(start));
  return clauses;
}

}  // namespace gaffer::query
