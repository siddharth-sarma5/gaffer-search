#include "query/query_parser.hpp"

#include <vector>

#include <gtest/gtest.h>

namespace gaffer::query {
namespace {

using Clauses = std::vector<Clause>;
constexpr auto kT = ClauseKind::kTerms;
constexpr auto kP = ClauseKind::kPhrase;

TEST(ParseQueryTest, PlainWordsAreOneTermsClause) {
  EXPECT_EQ(parse_query("haaland brace"), (Clauses{{kT, "haaland brace"}}));
}

TEST(ParseQueryTest, QuotedTextIsAPhrase) {
  EXPECT_EQ(parse_query("\"late winner\""), (Clauses{{kP, "late winner"}}));
}

TEST(ParseQueryTest, MixedClausesKeepTheirOrder) {
  EXPECT_EQ(
      parse_query("haaland \"late winner\" anfield \"stadium of light\""),
      (Clauses{
          {kT, "haaland "}, {kP, "late winner"}, {kT, " anfield "}, {kP, "stadium of light"}}));
}

TEST(ParseQueryTest, TypographicQuotes) {
  EXPECT_EQ(parse_query("“north london derby” arsenal"),
            (Clauses{{kP, "north london derby"}, {kT, " arsenal"}}));
}

TEST(ParseQueryTest, UnterminatedQuoteMakesTheRestAPhrase) {
  EXPECT_EQ(parse_query("arsenal \"late winner"), (Clauses{{kT, "arsenal "}, {kP, "late winner"}}));
}

TEST(ParseQueryTest, EmptyAndBlankClausesAreDropped) {
  EXPECT_TRUE(parse_query("").empty());
  EXPECT_TRUE(parse_query("   ").empty());
  EXPECT_TRUE(parse_query("\"\"").empty());
  EXPECT_EQ(parse_query("\"  \" goals"), (Clauses{{kT, " goals"}}));
  EXPECT_EQ(parse_query("\"a\"\"b\""), (Clauses{{kP, "a"}, {kP, "b"}}));
}

}  // namespace
}  // namespace gaffer::query
