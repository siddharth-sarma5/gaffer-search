#include "snippet/snippet.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "text/analyzer.hpp"

namespace gaffer::snippet {
namespace {

using Terms = std::vector<std::string>;

std::string md(const Snippet& s) { return render(s, "[", "]"); }

TEST(SnippetTest, HighlightsAllMatchesInShortText) {
  const text::Analyzer a;
  const Snippet s = make_snippet("Chiesa scored a late winner.", a, Terms{"late", "winner"});
  EXPECT_FALSE(s.cut_start);
  EXPECT_FALSE(s.cut_end);
  EXPECT_EQ(md(s), "Chiesa scored a [late] [winner].");
}

TEST(SnippetTest, PrefersWindowCoveringMoreDistinctTerms) {
  const text::Analyzer a;
  // "late" alone appears early and often; "late winner" together appears later.
  const std::string text =
      "late late late kickoff because of traffic and then a long delay before anything at all "
      "happened in the first half or the second half until finally Salah scored a late winner "
      "at the death.";
  const Snippet s = make_snippet(text, a, Terms{"late", "winner"}, {.window = 8, .lead_in = 2});
  EXPECT_TRUE(s.cut_start);
  EXPECT_NE(md(s).find("[late] [winner]"), std::string::npos) << md(s);
}

TEST(SnippetTest, HighlightsOriginalSpellingOfFoldedAndStemmedTerms) {
  const text::Analyzer a;
  const Snippet s =
      make_snippet("Martin Ødegaard missed two penalties.", a, Terms{"odegaard", "penalty"});
  EXPECT_EQ(md(s), "Martin [Ødegaard] missed two [penalties].");
}

TEST(SnippetTest, HighlightOffsetsPointIntoSnippetText) {
  const text::Analyzer a;
  const Snippet s =
      make_snippet("one two three four five six seven eight nine ten winner eleven twelve thirteen",
                   a, Terms{"winner"}, {.window = 4, .lead_in = 1});
  ASSERT_EQ(s.highlights.size(), 1U);
  EXPECT_EQ(s.text.substr(s.highlights[0].begin, s.highlights[0].end - s.highlights[0].begin),
            "winner");
  EXPECT_TRUE(s.cut_start);
  EXPECT_TRUE(s.cut_end);
  EXPECT_EQ(md(s), "... ten [winner] eleven twelve ...");  // positions 9..12 of 0..13
}

TEST(SnippetTest, NoMatchFallsBackToTheOpening) {
  const text::Analyzer a;
  const Snippet s =
      make_snippet("alpha beta gamma delta epsilon", a, Terms{"zeta"}, {.window = 3, .lead_in = 0});
  EXPECT_TRUE(s.highlights.empty());
  EXPECT_FALSE(s.cut_start);
  EXPECT_EQ(md(s), "alpha beta gamma ...");
}

TEST(SnippetTest, EmptyOrUnsearchableText) {
  const text::Analyzer a;
  EXPECT_EQ(make_snippet("", a, Terms{"x"}).text, "");
  EXPECT_EQ(make_snippet("the of at", a, Terms{"x"}).text, "the of at");
}

TEST(SnippetTest, NeverSplitsUtf8) {
  const text::Analyzer a;
  // Window of 1 around "ødegaard": the cut must land on token boundaries.
  const Snippet s =
      make_snippet("Ødegaard Ødegaard Ødegaard", a, Terms{"odegaard"}, {.window = 1, .lead_in = 0});
  EXPECT_EQ(s.text, "Ødegaard");
}

}  // namespace
}  // namespace gaffer::snippet
