#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gaffer::query {

enum class ClauseKind : std::uint8_t {
  kTerms,   // free words: any of them may match (OR), scored by BM25
  kPhrase,  // quoted words: must appear together, in order
};

// A raw piece of the query, before analysis.
struct Clause {
  ClauseKind kind;
  std::string text;

  friend bool operator==(const Clause&, const Clause&) = default;
};

// Splits a query into term and phrase clauses, in the order they appear:
//
//   haaland "late winner" anfield
//     -> [Terms "haaland "] [Phrase "late winner"] [Terms " anfield"]
//
// Rules:
//  * Straight quotes (") and typographic quotes (U+201C/U+201D) delimit phrases.
//  * An unterminated quote turns the rest of the query into a phrase; it is not
//    an error, because users shouldn't be punished for a missing quote.
//  * Clauses that are empty or only whitespace are dropped.
//
// The parser deals only with quoting. Words, case and stopwords are left to the
// Analyzer, so queries and documents share one definition of a "term".
[[nodiscard]] std::vector<Clause> parse_query(std::string_view query);

}  // namespace gaffer::query
