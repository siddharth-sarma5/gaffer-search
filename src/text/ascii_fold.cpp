#include "text/ascii_fold.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace gaffer::text {

namespace {

using namespace std::string_view_literals;

// "a".."z" and "0".."9", indexed by ASCII code: lets fold_code_point return a
// view into static storage instead of allocating.
constexpr std::string_view kAsciiLower = "abcdefghijklmnopqrstuvwxyz";
constexpr std::string_view kDigits = "0123456789";

// U+00C0..U+00FF. Empty = not a letter (U+00D7 multiplication sign, U+00F7 division sign).
constexpr std::array<std::string_view, 64> kLatin1{
    "a"sv, "a"sv, "a"sv, "a"sv, "a"sv, "a"sv, "ae"sv, "c"sv,   // C0-C7  ÀÁÂÃÄÅÆÇ
    "e"sv, "e"sv, "e"sv, "e"sv, "i"sv, "i"sv, "i"sv,  "i"sv,   // C8-CF  ÈÉÊËÌÍÎÏ
    "d"sv, "n"sv, "o"sv, "o"sv, "o"sv, "o"sv, "o"sv,  ""sv,    // D0-D7  ÐÑÒÓÔÕÖ×
    "o"sv, "u"sv, "u"sv, "u"sv, "u"sv, "y"sv, "th"sv, "ss"sv,  // D8-DF  ØÙÚÛÜÝÞß
    "a"sv, "a"sv, "a"sv, "a"sv, "a"sv, "a"sv, "ae"sv, "c"sv,   // E0-E7  àáâãäåæç
    "e"sv, "e"sv, "e"sv, "e"sv, "i"sv, "i"sv, "i"sv,  "i"sv,   // E8-EF  èéêëìíîï
    "d"sv, "n"sv, "o"sv, "o"sv, "o"sv, "o"sv, "o"sv,  ""sv,    // F0-F7  ðñòóôõö÷
    "o"sv, "u"sv, "u"sv, "u"sv, "u"sv, "y"sv, "th"sv, "y"sv,   // F8-FF  øùúûüýþÿ
};

// U+0100..U+017F: base letter for each code point (upper/lower pairs alternate).
constexpr std::array<std::string_view, 128> kLatinExtA{
    "a"sv, "a"sv, "a"sv,  "a"sv,  "a"sv, "a"sv, "c"sv, "c"sv,  // 0100 ĀāĂăĄąĆć
    "c"sv, "c"sv, "c"sv,  "c"sv,  "c"sv, "c"sv, "d"sv, "d"sv,  // 0108 ĈĉĊċČčĎď
    "d"sv, "d"sv, "e"sv,  "e"sv,  "e"sv, "e"sv, "e"sv, "e"sv,  // 0110 ĐđĒēĔĕĖė
    "e"sv, "e"sv, "e"sv,  "e"sv,  "g"sv, "g"sv, "g"sv, "g"sv,  // 0118 ĘęĚěĜĝĞğ
    "g"sv, "g"sv, "g"sv,  "g"sv,  "h"sv, "h"sv, "h"sv, "h"sv,  // 0120 ĠġĢģĤĥĦħ
    "i"sv, "i"sv, "i"sv,  "i"sv,  "i"sv, "i"sv, "i"sv, "i"sv,  // 0128 ĨĩĪīĬĭĮį
    "i"sv, "i"sv, "ij"sv, "ij"sv, "j"sv, "j"sv, "k"sv, "k"sv,  // 0130 İıĲĳĴĵĶķ
    "k"sv, "l"sv, "l"sv,  "l"sv,  "l"sv, "l"sv, "l"sv, "l"sv,  // 0138 ĸĹĺĻļĽľĿ
    "l"sv, "l"sv, "l"sv,  "n"sv,  "n"sv, "n"sv, "n"sv, "n"sv,  // 0140 ŀŁłŃńŅņŇ
    "n"sv, "n"sv, "n"sv,  "n"sv,  "o"sv, "o"sv, "o"sv, "o"sv,  // 0148 ňŉŊŋŌōŎŏ
    "o"sv, "o"sv, "oe"sv, "oe"sv, "r"sv, "r"sv, "r"sv, "r"sv,  // 0150 ŐőŒœŔŕŖŗ
    "r"sv, "r"sv, "s"sv,  "s"sv,  "s"sv, "s"sv, "s"sv, "s"sv,  // 0158 ŘřŚśŜŝŞş
    "s"sv, "s"sv, "t"sv,  "t"sv,  "t"sv, "t"sv, "t"sv, "t"sv,  // 0160 ŠšŢţŤťŦŧ
    "u"sv, "u"sv, "u"sv,  "u"sv,  "u"sv, "u"sv, "u"sv, "u"sv,  // 0168 ŨũŪūŬŭŮů
    "u"sv, "u"sv, "u"sv,  "u"sv,  "w"sv, "w"sv, "y"sv, "y"sv,  // 0170 ŰűŲųŴŵŶŷ
    "y"sv, "z"sv, "z"sv,  "z"sv,  "z"sv, "z"sv, "z"sv, "s"sv,  // 0178 ŸŹźŻżŽžſ
};

constexpr char32_t kLatin1First = 0x00C0;
constexpr char32_t kLatinExtAFirst = 0x0100;
constexpr char32_t kLatinExtALast = 0x017F;

constexpr bool is_continuation(unsigned char b) noexcept { return (b & 0xC0U) == 0x80U; }

}  // namespace

std::optional<char32_t> decode_utf8(std::string_view text, std::size_t& pos) noexcept {
  const auto lead = static_cast<unsigned char>(text[pos]);
  std::size_t len = 0;
  char32_t cp = 0;
  char32_t min = 0;  // smallest code point that legitimately needs `len` bytes
  if (lead < 0x80U) {
    ++pos;
    return lead;
  }
  if ((lead & 0xE0U) == 0xC0U) {
    len = 2;
    cp = lead & 0x1FU;
    min = 0x80;
  } else if ((lead & 0xF0U) == 0xE0U) {
    len = 3;
    cp = lead & 0x0FU;
    min = 0x800;
  } else if ((lead & 0xF8U) == 0xF0U) {
    len = 4;
    cp = lead & 0x07U;
    min = 0x10000;
  } else {
    ++pos;  // stray continuation byte or invalid lead byte
    return std::nullopt;
  }
  if (pos + len > text.size()) {
    ++pos;
    return std::nullopt;
  }
  for (std::size_t i = 1; i < len; ++i) {
    const auto b = static_cast<unsigned char>(text[pos + i]);
    if (!is_continuation(b)) {
      ++pos;
      return std::nullopt;
    }
    cp = (cp << 6U) | (b & 0x3FU);
  }
  // Overlong forms ("A" encoded in 2 bytes) are a classic filter-bypass trick;
  // surrogates and values past U+10FFFF are not valid scalar values.
  if (cp < min || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
    ++pos;
    return std::nullopt;
  }
  pos += len;
  return cp;
}

std::string_view fold_code_point(char32_t cp) noexcept {
  if (cp >= U'a' && cp <= U'z') {
    return kAsciiLower.substr(cp - U'a', 1);
  }
  if (cp >= U'A' && cp <= U'Z') {
    return kAsciiLower.substr(cp - U'A', 1);
  }
  if (cp >= U'0' && cp <= U'9') {
    return kDigits.substr(cp - U'0', 1);
  }
  if (cp >= kLatin1First && cp < kLatinExtAFirst) {
    return kLatin1.at(cp - kLatin1First);
  }
  if (cp >= kLatinExtAFirst && cp <= kLatinExtALast) {
    return kLatinExtA.at(cp - kLatinExtAFirst);
  }
  switch (cp) {
    case 0x0218:  // Ș
    case 0x0219:  // ș
      return "s"sv;
    case 0x021A:  // Ț
    case 0x021B:  // ț
      return "t"sv;
    default:
      return ""sv;
  }
}

}  // namespace gaffer::text
