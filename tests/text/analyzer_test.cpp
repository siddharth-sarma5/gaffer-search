#include "text/analyzer.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "text/stemmer.hpp"
#include "text/stopwords.hpp"

namespace gaffer::text {
namespace {

TEST(StopwordsTest, CommonWordsAreStopwords) {
  for (const std::string_view w : {"the", "at", "a", "of", "with", "was"}) {
    EXPECT_TRUE(is_stopword(w)) << w;
  }
}

TEST(StopwordsTest, FootballWordsAreKept) {
  for (const std::string_view w : {"off", "home", "away", "down", "no", "late", "winner", ""}) {
    EXPECT_FALSE(is_stopword(w)) << w;
  }
}

TEST(SStemTest, AppliesHarmanRules) {
  const std::vector<std::pair<std::string_view, std::string_view>> cases{
      {"penalties", "penalty"},  // rule 1
      {"braces", "brace"},       // rule 2
      {"goals", "goal"},         // rule 3
      {"wins", "win"},          {"cards", "card"},
  };
  for (const auto& [in, want] : cases) {
    EXPECT_EQ(s_stem(in), want) << in;
  }
}

TEST(SStemTest, Exceptions) {
  EXPECT_EQ(s_stem("bonus"), "bonus");  // -us
  EXPECT_EQ(s_stem("cross"), "cross");  // -ss
  EXPECT_EQ(s_stem("toes"), "toe");     // -oes -> rule 3 ("es" exception, then plain "s")
  EXPECT_EQ(s_stem("bus"), "bus");      // shorter than 4
  EXPECT_EQ(s_stem("90s"), "90s");      // contains a digit
  EXPECT_EQ(s_stem("salah"), "salah");
}

TEST(SStemTest, KnownLimitationEsAfterConsonantCluster) {
  // Rule 2 keeps the 'e', so "matches" and "match" don't conflate. This is a
  // documented weakness of the S stemmer, pinned here so a change is deliberate.
  EXPECT_EQ(s_stem("matches"), "matche");
  EXPECT_EQ(s_stem("match"), "match");
}

TEST(SStemTest, IsIdempotentOnItsOutput) {
  for (const std::string_view w : {"penalties", "goals", "braces", "matches", "clubs"}) {
    const std::string once = s_stem(w);
    EXPECT_EQ(s_stem(once), once) << w;
  }
}

std::vector<std::pair<std::string, std::uint32_t>> analyzed(const Analyzer& a,
                                                            std::string_view text) {
  std::vector<std::pair<std::string, std::uint32_t>> out;
  for (const auto& t : a.analyze(text)) {
    out.emplace_back(t.term, t.position);
  }
  return out;
}

TEST(AnalyzerTest, FullPipelineKeepsPositionGaps) {
  const Analyzer a;
  using P = std::vector<std::pair<std::string, std::uint32_t>>;
  EXPECT_EQ(analyzed(a, "Late winners at Anfield"),
            (P{{"late", 0}, {"winner", 1}, {"anfield", 3}}));
}

TEST(AnalyzerTest, OptionsDisableStagesAndQueriesMatchDocuments) {
  const Analyzer raw(AnalyzerOptions{.remove_stopwords = false, .stem = false});
  using P = std::vector<std::pair<std::string, std::uint32_t>>;
  EXPECT_EQ(analyzed(raw, "the Goals"), (P{{"the", 0}, {"goals", 1}}));

  // The property the whole engine relies on: query and document spellings meet.
  const Analyzer a;
  EXPECT_EQ(a.analyze("PENALTIES")[0].term, a.analyze("penalty")[0].term);
  EXPECT_EQ(a.analyze("Ødegaard's")[0].term, a.analyze("odegaards")[0].term);
}

}  // namespace
}  // namespace gaffer::text
