#include "text/tokenizer.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "text/ascii_fold.hpp"

namespace gaffer::text {

namespace {

constexpr char32_t kRightSingleQuote = 0x2019;

bool is_apostrophe(char32_t cp) noexcept { return cp == U'\'' || cp == kRightSingleQuote; }

// Peeks at the code point starting at `pos` without consuming it.
std::optional<char32_t> peek(std::string_view text, std::size_t pos) noexcept {
  if (pos >= text.size()) {
    return std::nullopt;
  }
  return decode_utf8(text, pos);
}

}  // namespace

std::vector<Token> tokenize(std::string_view text) {
  // Offsets are stored as uint32 to keep Token small; documents are far smaller than 4 GiB.
  assert(text.size() <= std::numeric_limits<std::uint32_t>::max());

  std::vector<Token> tokens;
  std::string current;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  std::uint32_t position = 0;

  auto flush = [&] {
    if (!current.empty() && current.size() <= kMaxTokenBytes) {
      tokens.push_back(Token{std::move(current), position++,
                             static_cast<std::uint32_t>(token_begin),
                             static_cast<std::uint32_t>(token_end)});
    }
    current.clear();  // valid after move: clear() puts it in a known empty state
  };

  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t start = pos;
    const std::optional<char32_t> cp = decode_utf8(text, pos);  // advances pos
    const std::string_view folded = cp ? fold_code_point(*cp) : std::string_view{};

    if (!folded.empty()) {
      if (current.empty()) {
        token_begin = start;
      }
      current.append(folded);
      token_end = pos;
      continue;
    }
    // Apostrophe inside a word: skip it and keep building the same term.
    if (cp && is_apostrophe(*cp) && !current.empty()) {
      const auto next = peek(text, pos);
      if (next && !fold_code_point(*next).empty()) {
        continue;
      }
    }
    flush();
  }
  flush();
  return tokens;
}

}  // namespace gaffer::text
