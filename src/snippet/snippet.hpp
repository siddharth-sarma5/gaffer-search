#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "text/analyzer.hpp"

namespace gaffer::snippet {

// A byte range [begin, end) within Snippet::text to highlight.
struct Highlight {
  std::size_t begin;
  std::size_t end;

  friend bool operator==(const Highlight&, const Highlight&) = default;
};

struct Snippet {
  std::string text;                   // a verbatim excerpt of the original text
  std::vector<Highlight> highlights;  // ascending, non-overlapping
  bool cut_start = false;             // the excerpt doesn't start at the beginning
  bool cut_end = false;               // ...or doesn't run to the end
};

struct SnippetOptions {
  std::uint32_t window = 24;  // excerpt length, in token positions (stopwords count)
  std::uint32_t lead_in = 3;  // positions of context kept before the first match
};

// Picks the most query-dense passage of `text` and marks the query terms in it.
//
//  1. Analyse `text` with the index's Analyzer. Tokens keep their byte offsets
//     into the original text, so "Ødegaard" can be highlighted for the query
//     "odegaard", and "penalties" for "penalty".
//  2. Try a window starting `lead_in` positions before each matching token;
//     score it as (distinct query terms covered, total matches) and keep the
//     best (earliest on ties). Distinct terms come first: a passage with
//     "late" AND "winner" beats one that says "late" three times.
//  3. Cut the excerpt at token boundaries, so it never splits a UTF-8 sequence.
// With no matches, the opening of the text is returned (search engines show a
// lead paragraph rather than nothing).
//
// `query_terms` are analysed terms (as in the index), e.g. {"late", "winner"}.
[[nodiscard]] Snippet make_snippet(std::string_view text, const text::Analyzer& analyzer,
                                   const std::vector<std::string>& query_terms,
                                   SnippetOptions options = {});

// Renders a snippet with markers around highlights and "..." at cut ends, e.g.
// render(s, "**", "**") -> "... Federico Chiesa scored a **late** **winner** for ...".
[[nodiscard]] std::string render(const Snippet& snippet, std::string_view open,
                                 std::string_view close);

}  // namespace gaffer::snippet
