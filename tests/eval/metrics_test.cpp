#include "eval/metrics.hpp"

#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.hpp"

namespace gaffer::eval {
namespace {

const std::vector<std::string> kRanked{"a", "x", "b", "y", "z", "c"};
const std::set<std::string> kRelevant{"a", "b", "c", "d"};

TEST(MetricsTest, PrecisionUsesKAsDenominator) {
  EXPECT_DOUBLE_EQ(precision_at(kRanked, kRelevant, 1), 1.0);
  EXPECT_DOUBLE_EQ(precision_at(kRanked, kRelevant, 5), 2.0 / 5.0);
  // Only 6 results returned, but P@10 still divides by 10.
  EXPECT_DOUBLE_EQ(precision_at(kRanked, kRelevant, 10), 3.0 / 10.0);
  EXPECT_DOUBLE_EQ(precision_at(kRanked, kRelevant, 0), 0.0);
}

TEST(MetricsTest, RecallDividesByRelevantCount) {
  EXPECT_DOUBLE_EQ(recall_at(kRanked, kRelevant, 3), 2.0 / 4.0);
  EXPECT_DOUBLE_EQ(recall_at(kRanked, kRelevant, 10), 3.0 / 4.0);  // "d" never retrieved
  EXPECT_DOUBLE_EQ(recall_at(kRanked, {}, 10), 0.0);
}

TEST(MetricsTest, ReciprocalRankOfFirstRelevant) {
  EXPECT_DOUBLE_EQ(reciprocal_rank(kRanked, kRelevant), 1.0);
  EXPECT_DOUBLE_EQ(reciprocal_rank({"x", "y", "b"}, kRelevant), 1.0 / 3.0);
  EXPECT_DOUBLE_EQ(reciprocal_rank({"x", "y"}, kRelevant), 0.0);
  EXPECT_DOUBLE_EQ(reciprocal_rank({}, kRelevant), 0.0);
}

TEST(LoadEvalFilesTest, CommittedQueriesAndQrelsAgree) {
  const auto queries = load_queries("eval/queries.tsv");
  const auto qrels = load_qrels("eval/qrels.tsv");
  EXPECT_EQ(queries.size(), 20U);
  for (const auto& q : queries) {
    ASSERT_TRUE(qrels.contains(q.id)) << q.id;
    EXPECT_FALSE(qrels.at(q.id).empty()) << q.id;
    EXPECT_FALSE(q.criterion.empty()) << q.id;
  }
  EXPECT_THROW((void)load_qrels("nope.tsv"), IoError);
}

}  // namespace
}  // namespace gaffer::eval
