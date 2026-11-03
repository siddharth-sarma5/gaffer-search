#include "query/nicknames.hpp"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.hpp"

namespace gaffer::query {
namespace {

using Found = std::vector<Expansion>;

NicknameExpander small() {
  return NicknameExpander({{"gunners", "Arsenal"},
                           {"reds", "Liverpool"},
                           {"man", "Manchester"},  // shorter alias to test longest-first
                           {"man utd", "Manchester United"},
                           {"red devils", "Manchester United"}});
}

TEST(NicknameExpanderTest, FindsSingleWordAliasCaseInsensitively) {
  EXPECT_EQ(small().find("The GUNNERS late winner"), (Found{{"gunners", "Arsenal"}}));
}

TEST(NicknameExpanderTest, MultiWordAliasesAndLongestMatchFirst) {
  EXPECT_EQ(small().find("man utd v red devils"),
            (Found{{"man utd", "Manchester United"}, {"red devils", "Manchester United"}}));
  EXPECT_EQ(small().find("man city"), (Found{{"man", "Manchester"}}));
}

TEST(NicknameExpanderTest, MatchesRawTokensNotStems) {
  // "reds" -> Liverpool, but "red card" must not expand (the stemmer would map
  // "reds" to "red"; matching happens before stemming).
  EXPECT_EQ(small().find("reds"), (Found{{"reds", "Liverpool"}}));
  EXPECT_TRUE(small().find("red card").empty());
  EXPECT_TRUE(small().find("gunner").empty());  // singular isn't an alias
}

TEST(NicknameExpanderTest, PunctuationDoesNotMatter) {
  EXPECT_EQ(small().find("\"Man-Utd!\""), (Found{{"man utd", "Manchester United"}}));
}

TEST(NicknameExpanderTest, LoadTsvSkipsCommentsAndRejectsBadLines) {
  std::istringstream ok("# comment\n\ngunners\tArsenal\r\n");
  EXPECT_EQ(NicknameExpander::load_tsv(ok, "n.tsv").size(), 1U);

  for (const std::string bad : {"gunners Arsenal\n", "\tArsenal\n", "gunners\t\n", "a\tb\tc\n",
                                "gunners\tA\nGUNNERS\tB\n", "!!!\tX\n"}) {
    std::istringstream in(bad);
    EXPECT_THROW((void)NicknameExpander::load_tsv(in, "n.tsv"), ParseError) << bad;
  }
}

TEST(NicknameExpanderTest, CommittedFileLoads) {
  const auto e = NicknameExpander::load_tsv_file("data/nicknames.tsv");
  EXPECT_GE(e.size(), 20U);
  EXPECT_EQ(e.find("toffees"), (Found{{"toffees", "Everton"}}));
  EXPECT_THROW((void)NicknameExpander::load_tsv_file("nope.tsv"), IoError);
}

}  // namespace
}  // namespace gaffer::query
