#include "query/top_k.hpp"

#include <algorithm>
#include <random>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace gaffer::query {
namespace {

std::vector<DocId> ids(const std::vector<ScoredDoc>& v) {
  std::vector<DocId> out;
  for (const auto& d : v) {
    out.push_back(d.doc);
  }
  return out;
}

TEST(RanksBeforeTest, HigherScoreThenLowerDocId) {
  EXPECT_TRUE(ranks_before({5, 2.0}, {1, 1.0}));
  EXPECT_FALSE(ranks_before({1, 1.0}, {5, 2.0}));
  EXPECT_TRUE(ranks_before({1, 1.0}, {2, 1.0}));   // tie -> lower id first
  EXPECT_FALSE(ranks_before({2, 1.0}, {2, 1.0}));  // irreflexive
}

TEST(TopKTest, KeepsTheBestKInRankOrder) {
  TopK top(3);
  for (const ScoredDoc d : {ScoredDoc{0, 0.5}, {1, 3.0}, {2, 1.0}, {3, 2.0}, {4, 0.1}}) {
    top.push(d);
  }
  EXPECT_EQ(ids(std::move(top).take_sorted()), (std::vector<DocId>{1, 3, 2}));
}

TEST(TopKTest, TiesBreakByDocIdRegardlessOfArrivalOrder) {
  TopK top(2);
  for (const DocId d : {DocId{9}, DocId{3}, DocId{7}, DocId{1}}) {
    top.push({d, 1.0});
  }
  EXPECT_EQ(ids(std::move(top).take_sorted()), (std::vector<DocId>{1, 3}));
}

TEST(TopKTest, FewerCandidatesThanKAndKZero) {
  TopK top(10);
  top.push({4, 1.0});
  top.push({2, 2.0});
  EXPECT_EQ(ids(std::move(top).take_sorted()), (std::vector<DocId>{2, 4}));

  TopK none(0);
  none.push({1, 1.0});
  EXPECT_EQ(none.size(), 0U);
  EXPECT_TRUE(std::move(none).take_sorted().empty());
}

// Property test: for random inputs, TopK equals "sort everything, take k".
TEST(TopKTest, MatchesFullSortOnRandomInput) {
  std::mt19937 rng(440);                            // fixed seed: deterministic test
  std::uniform_int_distribution<int> score(0, 50);  // small range forces many ties
  for (int trial = 0; trial < 200; ++trial) {
    std::vector<ScoredDoc> all;
    const auto n = static_cast<DocId>(rng() % 100);
    for (DocId d = 0; d < n; ++d) {
      all.push_back({d, static_cast<double>(score(rng))});
    }
    std::ranges::shuffle(all, rng);
    const std::size_t k = rng() % 15;

    TopK top(k);
    for (const auto& d : all) {
      top.push(d);
    }
    std::ranges::sort(all, ranks_before);
    all.resize(std::min(k, all.size()));
    EXPECT_EQ(ids(std::move(top).take_sorted()), ids(all)) << "trial " << trial;
  }
}

}  // namespace
}  // namespace gaffer::query
