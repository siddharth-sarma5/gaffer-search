#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gaffer::text {

// A token as it appears in the text, before stopwords or stemming.
struct Token {
  std::string term;        // lower-case, ASCII-folded
  std::uint32_t position;  // 0, 1, 2, ... in order of appearance (used by phrase queries)
  std::uint32_t begin;     // byte offsets into the original text, [begin, end),
  std::uint32_t end;       // so snippets can highlight the original spelling
};

// Tokens longer than this are dropped: they're junk (hashes, run-together
// text) and would bloat the term dictionary.
inline constexpr std::size_t kMaxTokenBytes = 64;

// Splits UTF-8 text into terms.
//
// Rules:
//  * A term is a maximal run of letters and digits. Latin letters with
//    diacritics are folded to ASCII (see ascii_fold.hpp) and everything is
//    lower-cased: "Ødegaard" -> "odegaard".
//  * Everything else separates terms: spaces, punctuation, hyphens
//    ("hat-trick" -> "hat", "trick"), "+" and "'" in minutes
//    ("90+4'" -> "90", "4"), and any non-Latin code point.
//  * An apostrophe between two letters is dropped, not split on:
//    "O'Riley" -> "oriley". A trailing possessive ("James'") just ends the term.
//    Both ' and U+2019 (typographic apostrophe) are handled.
//  * Invalid UTF-8 bytes act as separators. Tokenizing never fails.
//
// The same function runs over documents and queries, so the two always agree.
[[nodiscard]] std::vector<Token> tokenize(std::string_view text);

}  // namespace gaffer::text
