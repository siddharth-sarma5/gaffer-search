#include "text/tokenizer.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "text/ascii_fold.hpp"

namespace gaffer::text {
namespace {

std::vector<std::string> terms(std::string_view text) {
  std::vector<std::string> out;
  for (const auto& t : tokenize(text)) {
    out.push_back(t.term);
  }
  return out;
}

using Terms = std::vector<std::string>;

TEST(DecodeUtf8Test, DecodesOneToFourByteSequences) {
  const std::string s = "A\xC3\xA9\xE2\x80\x99\xF0\x9F\x98\x80";  // A é ’ 😀
  std::size_t pos = 0;
  EXPECT_EQ(decode_utf8(s, pos), U'A');
  EXPECT_EQ(decode_utf8(s, pos), char32_t{0xE9});
  EXPECT_EQ(decode_utf8(s, pos), char32_t{0x2019});
  EXPECT_EQ(decode_utf8(s, pos), char32_t{0x1F600});
  EXPECT_EQ(pos, s.size());
}

TEST(DecodeUtf8Test, InvalidInputConsumesOneByte) {
  for (const std::string& bad : {
           std::string("\x80"),              // stray continuation byte
           std::string("\xC3"),              // truncated 2-byte sequence
           std::string("\xC3(", 2),          // lead byte not followed by a continuation
           std::string("\xC0\x81"),          // overlong encoding of U+0001
           std::string("\xED\xA0\x80"),      // UTF-16 surrogate U+D800
           std::string("\xF4\x90\x80\x80"),  // above U+10FFFF
           std::string("\xFF"),
       }) {
    std::size_t pos = 0;
    EXPECT_FALSE(decode_utf8(bad, pos).has_value()) << testing::PrintToString(bad);
    EXPECT_EQ(pos, 1U);
  }
}

TEST(FoldTest, FoldsLatinLettersAndRejectsOthers) {
  EXPECT_EQ(fold_code_point(U'Q'), "q");
  EXPECT_EQ(fold_code_point(U'7'), "7");
  EXPECT_EQ(fold_code_point(0x00D8), "o");   // Ø
  EXPECT_EQ(fold_code_point(0x00DF), "ss");  // ß
  EXPECT_EQ(fold_code_point(0x0141), "l");   // Ł
  EXPECT_EQ(fold_code_point(0x0153), "oe");  // œ
  EXPECT_EQ(fold_code_point(0x0219), "s");   // ș
  EXPECT_EQ(fold_code_point(0x00D7), "");    // × is not a letter
  EXPECT_EQ(fold_code_point(U'-'), "");
  EXPECT_EQ(fold_code_point(0x4E2D), "");  // CJK: not folded, acts as a separator
}

TEST(TokenizeTest, LowercasesAndSplitsOnPunctuation) {
  EXPECT_EQ(terms("Liverpool beat AFC Bournemouth 4-2 at home."),
            (Terms{"liverpool", "beat", "afc", "bournemouth", "4", "2", "at", "home"}));
}

TEST(TokenizeTest, FoldsDiacritics) {
  EXPECT_EQ(terms("Martin Ødegaard, Viktor Gyökeres, Łukasz Fabiański"),
            (Terms{"martin", "odegaard", "viktor", "gyokeres", "lukasz", "fabianski"}));
}

TEST(TokenizeTest, HyphensAndMinutesSplit) {
  EXPECT_EQ(terms("hat-trick 90+4' Jean-Philippe"),
            (Terms{"hat", "trick", "90", "4", "jean", "philippe"}));
}

TEST(TokenizeTest, ApostropheInsideWordIsDropped) {
  EXPECT_EQ(terms("Matt O'Riley, St. James' Park"),
            (Terms{"matt", "oriley", "st", "james", "park"}));
  EXPECT_EQ(terms("O\xE2\x80\x99Riley"), (Terms{"oriley"}));  // typographic apostrophe
  EXPECT_EQ(terms("'quoted'"), (Terms{"quoted"}));
}

TEST(TokenizeTest, PositionsAndByteOffsets) {
  const std::string text = "Ødegaard scores";
  const auto toks = tokenize(text);
  ASSERT_EQ(toks.size(), 2U);
  EXPECT_EQ(toks[0].position, 0U);
  EXPECT_EQ(toks[1].position, 1U);
  // Offsets point at the original (unfolded) bytes: Ø is 2 bytes in UTF-8.
  EXPECT_EQ(text.substr(toks[0].begin, toks[0].end - toks[0].begin), "Ødegaard");
  EXPECT_EQ(text.substr(toks[1].begin, toks[1].end - toks[1].begin), "scores");
}

TEST(TokenizeTest, EdgeCases) {
  EXPECT_TRUE(tokenize("").empty());
  EXPECT_TRUE(tokenize(" ,.;-- ").empty());
  EXPECT_EQ(terms("a\xFF"
                  "b"),
            (Terms{"a", "b"}));  // invalid byte separates
  EXPECT_EQ(terms(std::string(kMaxTokenBytes + 1, 'x') + " ok"), (Terms{"ok"}));
  EXPECT_EQ(terms(std::string(kMaxTokenBytes, 'x')).size(), 1U);
}

}  // namespace
}  // namespace gaffer::text
