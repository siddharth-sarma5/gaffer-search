#pragma once

#include <optional>
#include <string_view>

namespace gaffer::text {

// Decodes one UTF-8 code point starting at text[pos] and advances pos past it.
// Invalid input (bad lead byte, truncated sequence, overlong encoding,
// surrogate, > U+10FFFF) consumes exactly one byte and returns std::nullopt,
// so a bad byte can never swallow the valid characters after it.
[[nodiscard]] std::optional<char32_t> decode_utf8(std::string_view text, std::size_t& pos) noexcept;

// Lower-case ASCII spelling of a Latin letter, or "" if `cp` isn't one we fold.
//   'A'..'Z' -> "a".."z", 'a'..'z' and '0'..'9' unchanged
//   U+00C0..U+017F (Latin-1 Supplement + Latin Extended-A) -> base letter(s):
//       'É' -> "e", 'Ø' -> "o", 'ß' -> "ss", 'Ł' -> "l", 'Œ' -> "oe"
//   U+0218..U+021B (Romanian comma-below Ș ș Ț ț) -> "s"/"t"
// So "Ødegaard", "Gyökeres" and "Fabiański" are indexed as "odegaard",
// "gyokeres" and "fabianski", and queries typed without accents match.
[[nodiscard]] std::string_view fold_code_point(char32_t cp) noexcept;

}  // namespace gaffer::text
