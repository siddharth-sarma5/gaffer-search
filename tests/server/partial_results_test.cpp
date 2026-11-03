// Partial results: slow and dead shards degrade completeness, not latency.

#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "cluster.hpp"
#include "corpus/corpus.hpp"
#include "net/messages.hpp"
#include "net/rpc.hpp"
#include "query/nicknames.hpp"

namespace gaffer::server {
namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

class PartialResultsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    corpus_ = corpus::load_tsv_file("data/corpus/pl-2025-26.tsv");
    expander_ = query::NicknameExpander::load_tsv_file("data/nicknames.tsv");
  }
  corpus::Corpus corpus_;
  query::NicknameExpander expander_;
};

TEST_F(PartialResultsTest, SlowShardIsDroppedAtTheDeadline) {
  // Shard 1 takes 600 ms; the broker's budget is 150 ms.
  const testing::Cluster cluster(corpus_, 3, expander_,
                                 {.shard_deadline = 150ms, .delays = {0ms, 600ms, 0ms}});
  const auto t0 = Clock::now();
  const auto r = cluster.broker->search(net::SearchRequest{"late winner", 10});
  const auto took = Clock::now() - t0;

  EXPECT_TRUE(r.partial());
  EXPECT_EQ(r.missing_shards, (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(r.shards_total, 3U);
  EXPECT_FALSE(r.hits.empty());
  for (const auto& h : r.hits) {
    EXPECT_NE(h.shard, 1U);
  }
  EXPECT_GE(took, 140ms);
  EXPECT_LT(took, 450ms);  // deadline + generous scheduling margin, far below the 600 ms delay
}

TEST_F(PartialResultsTest, DeadShardIsReportedAndOthersStillAnswer) {
  testing::Cluster cluster(corpus_, 3, expander_);
  const auto full = cluster.broker->search(net::SearchRequest{"salah", 10});
  ASSERT_FALSE(full.partial());

  cluster.shards[2]->stop();  // the process "dies"
  const auto r = cluster.broker->search(net::SearchRequest{"salah", 10});
  EXPECT_EQ(r.missing_shards, (std::vector<std::uint32_t>{2}));
  EXPECT_LT(r.matched, full.matched);
  for (const auto& h : r.hits) {
    EXPECT_NE(h.shard, 2U);
  }
}

TEST_F(PartialResultsTest, AllShardsDownGivesEmptyPartialResponseNotAnError) {
  testing::Cluster cluster(corpus_, 2, expander_);
  for (auto& s : cluster.shards) {
    s->stop();
  }
  const auto r = cluster.broker->search(net::SearchRequest{"salah", 10});
  EXPECT_EQ(r.missing_shards, (std::vector<std::uint32_t>{0, 1}));
  EXPECT_TRUE(r.hits.empty());
}

TEST_F(PartialResultsTest, PartialFlagSurvivesTheWireToTheClient) {
  const testing::Cluster cluster(corpus_, 2, expander_,
                                 {.shard_deadline = 100ms, .delays = {400ms, 0ms}});
  const auto reply = net::call("127.0.0.1", cluster.broker->port(),
                               net::SearchRequest{"arsenal", 5}, net::Deadline::after(5s));
  ASSERT_TRUE(reply.has_value());
  const auto& r = std::get<net::SearchResponse>(*reply);
  EXPECT_TRUE(r.partial());
  EXPECT_EQ(r.missing_shards, (std::vector<std::uint32_t>{0}));
}

}  // namespace
}  // namespace gaffer::server
