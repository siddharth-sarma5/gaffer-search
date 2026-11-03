#include "server/broker.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "cluster.hpp"
#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "net/messages.hpp"
#include "net/rpc.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "query/top_k.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {
namespace {

using namespace std::chrono_literals;

TEST(ParseAddressTest, Valid) {
  const auto a = parse_address("shard0:7001");
  EXPECT_EQ(a.host, "shard0");
  EXPECT_EQ(a.port, 7001);
}

TEST(ParseAddressTest, Invalid) {
  for (const char* bad : {"nohost", ":7000", "host:", "host:0", "host:70000", "host:abc"}) {
    EXPECT_THROW((void)parse_address(bad), Error) << bad;
  }
}

std::vector<DocId> ids(const std::vector<net::Hit>& hits) {
  std::vector<DocId> out;
  for (const auto& h : hits) {
    out.push_back(h.doc);
  }
  return out;
}

// Property test: the heap merge equals "concatenate everything, sort, take k".
TEST(MergeTopKTest, MatchesFullSort) {
  std::mt19937 rng(7);
  for (int trial = 0; trial < 300; ++trial) {
    const std::size_t shards = 1 + rng() % 6;
    std::vector<std::vector<net::Hit>> lists(shards);
    std::vector<net::Hit> all;
    DocId next = 0;
    for (auto& list : lists) {
      for (std::size_t i = 0; i < rng() % 12; ++i) {
        net::Hit h;
        h.doc = next++;
        h.score = static_cast<double>(rng() % 8);  // many ties across shards
        list.push_back(h);
        all.push_back(h);
      }
      std::ranges::sort(list, [](const net::Hit& a, const net::Hit& b) {
        return query::ranks_before({a.doc, a.score}, {b.doc, b.score});
      });
    }
    std::ranges::sort(all, [](const net::Hit& a, const net::Hit& b) {
      return query::ranks_before({a.doc, a.score}, {b.doc, b.score});
    });
    const std::size_t k = rng() % 15;
    all.resize(std::min(k, all.size()));
    EXPECT_EQ(ids(merge_top_k(lists, k)), ids(all)) << "trial " << trial;
  }
}

class BrokerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    corpus_ = corpus::load_tsv_file("data/corpus/pl-2025-26.tsv");
    expander_ = query::NicknameExpander::load_tsv_file("data/nicknames.tsv");
  }
  corpus::Corpus corpus_;
  query::NicknameExpander expander_;
};

TEST_F(BrokerTest, ClusterOverTcpMatchesSingleNode) {
  const testing::Cluster cluster(corpus_, 3, expander_);
  EXPECT_EQ(cluster.broker->global_stats().doc_count, corpus_.size());

  const auto idx = index::build_index(corpus_, text::Analyzer{});
  const query::Searcher single(idx, {}, &expander_);
  for (const std::string q :
       {"late winner at anfield", "gunners \"late winner\"", "haaland brace"}) {
    // Through the broker's real TCP listener, as a client would.
    const auto reply = net::call("127.0.0.1", cluster.broker->port(), net::SearchRequest{q, 10},
                                 net::Deadline::after(5s));
    ASSERT_TRUE(reply.has_value()) << q;
    const auto& r = std::get<net::SearchResponse>(*reply);
    const auto ref = single.search(q, 10);
    EXPECT_EQ(r.matched, ref.matched) << q;
    EXPECT_FALSE(r.partial());
    ASSERT_EQ(r.hits.size(), ref.hits.size()) << q;
    for (std::size_t i = 0; i < r.hits.size(); ++i) {
      EXPECT_EQ(r.hits[i].doc, ref.hits[i].doc) << q << " rank " << i;
      EXPECT_EQ(r.hits[i].score, ref.hits[i].score) << q << " rank " << i;
      EXPECT_EQ(r.hits[i].key, corpus_[ref.hits[i].doc].key);
    }
  }
}

TEST_F(BrokerTest, ConcurrentClientsShareThePools) {
  const testing::Cluster cluster(corpus_, 2, expander_);
  std::vector<std::jthread> clients;
  std::atomic<int> ok{0};
  for (int c = 0; c < 8; ++c) {
    clients.emplace_back([&] {
      for (int i = 0; i < 25; ++i) {
        const auto r = cluster.broker->search(net::SearchRequest{"salah penalty", 5});
        ok += r.hits.empty() ? 0 : 1;
      }
    });
  }
  clients.clear();  // join
  EXPECT_EQ(ok.load(), 200);
}

TEST_F(BrokerTest, UnreachableShardAtStartupFailsAfterTimeout) {
  BrokerOptions bo;
  bo.shards = {{"127.0.0.1", 1}};  // nothing listens on port 1
  bo.startup_timeout = 300ms;
  Broker b(expander_, bo);
  EXPECT_THROW(b.start(), Error);
}

TEST_F(BrokerTest, UnsupportedRequestGetsError) {
  const testing::Cluster cluster(corpus_, 1, expander_);
  EXPECT_TRUE(
      std::holds_alternative<net::ErrorMessage>(cluster.broker->handle(net::StatsRequest{})));
}

}  // namespace
}  // namespace gaffer::server
