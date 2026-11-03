#include "server/global_stats.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "eval/metrics.hpp"
#include "index/inverted_index.hpp"
#include "index/partition.hpp"
#include "net/messages.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "server/shard_server.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {
namespace {

TEST(MergeStatsTest, SumsCountsAndDfs) {
  const auto g = merge_stats(
      {{0, 2, 10, 100, 3, {{"a", 2}, {"b", 1}}}, {1, 2, 5, 40, 3, {{"a", 1}, {"c", 4}}}});
  EXPECT_EQ(g.doc_count, 15U);
  EXPECT_EQ(g.total_length, 140U);
  EXPECT_EQ(g.doc_freq("a"), 3U);
  EXPECT_EQ(g.doc_freq("c"), 4U);
  EXPECT_EQ(g.doc_freq("zzz"), 0U);
  EXPECT_EQ(df_for(g, {"a", "zzz"}), (std::vector<net::TermDf>{{"a", 3}, {"zzz", 0}}));
}

TEST(MergeStatsTest, RejectsInconsistentClusters) {
  EXPECT_THROW((void)merge_stats({}), Error);
  EXPECT_THROW((void)merge_stats({{0, 3, 1, 1, 3, {}}, {1, 3, 1, 1, 3, {}}}), Error);  // 2 of 3
  EXPECT_THROW((void)merge_stats({{0, 2, 1, 1, 3, {}}, {0, 2, 1, 1, 3, {}}}), Error);  // dup id
  EXPECT_THROW((void)merge_stats({{0, 2, 1, 1, 3, {}}, {1, 2, 1, 1, 1, {}}}), Error);  // analyzer
}

TEST(AnalyzerFlagsTest, RoundTrip) {
  for (const bool stem : {false, true}) {
    for (const bool stop : {false, true}) {
      const auto o = analyzer_options(analyzer_flags({.remove_stopwords = stop, .stem = stem}));
      EXPECT_EQ(o.stem, stem);
      EXPECT_EQ(o.remove_stopwords, stop);
    }
  }
}

// --- the property the whole distributed design rests on -------------------

class ShardedScoresTest : public ::testing::Test {
 protected:
  static constexpr std::uint32_t kShards = 3;

  static void SetUpTestSuite() {
    corpus_ = std::make_unique<corpus::Corpus>(corpus::load_tsv_file("data/corpus/pl-2025-26.tsv"));
    single_ =
        std::make_unique<index::InvertedIndex>(index::build_index(*corpus_, text::Analyzer{}));
    expander_ = std::make_unique<query::NicknameExpander>(
        query::NicknameExpander::load_tsv_file("data/nicknames.tsv"));
    auto parts = index::partition(*corpus_, kShards);
    for (std::uint32_t s = 0; s < kShards; ++s) {
      auto idx = index::build_index(parts[s], text::Analyzer{});
      shards_.push_back(std::make_unique<ShardServer>(
          std::move(parts[s]), std::move(idx), *expander_,
          ShardServerOptions{.shard_id = s, .num_shards = kShards, .tcp = {}}));
    }
  }
  static void TearDownTestSuite() {
    shards_.clear();
    expander_.reset();
    single_.reset();
    corpus_.reset();
  }

  static query::GlobalStats global() {
    std::vector<net::StatsResponse> stats;
    for (const auto& s : shards_) {
      stats.push_back(std::get<net::StatsResponse>(s->handle(net::StatsRequest{})));
    }
    return merge_stats(stats);
  }

  static std::vector<std::string> queries() {
    std::vector<std::string> qs;
    for (const auto& q : eval::load_queries("eval/queries.tsv")) {
      qs.push_back(q.text);
    }
    for (const char* q : {"\"late winner\" gunners", "\"north london derby\"",
                          "\"stadium of light\" clean sheet", "salah salah penalty"}) {
      qs.emplace_back(q);
    }
    return qs;
  }

  static inline std::unique_ptr<corpus::Corpus> corpus_;
  static inline std::unique_ptr<index::InvertedIndex> single_;
  static inline std::unique_ptr<query::NicknameExpander> expander_;
  static inline std::vector<std::unique_ptr<ShardServer>> shards_;
};

TEST_F(ShardedScoresTest, MergedStatsEqualSingleNodeStats) {
  const auto g = global();
  EXPECT_EQ(g.doc_count, single_->stats().doc_count);
  EXPECT_EQ(g.total_length, single_->stats().total_length);
  EXPECT_EQ(g.avg_doc_length(), single_->stats().avg_doc_length());  // bit-identical
  EXPECT_EQ(g.df.size(), single_->term_count());
  for (const auto term : single_->sorted_terms()) {
    ASSERT_EQ(g.doc_freq(term), single_->doc_freq(term)) << term;
  }
}

TEST_F(ShardedScoresTest, EveryShardHitScoresExactlyAsOnASingleNode) {
  const auto g = global();
  const query::Searcher single(*single_, {}, expander_.get());
  const text::Analyzer analyzer;
  std::size_t checked = 0;
  for (const auto& q : queries()) {
    std::map<DocId, double> reference;
    for (const auto& h : single.search(q, corpus_->size()).hits) {
      reference[h.doc] = h.score;
    }
    const auto plan = query::plan_query(q, analyzer, expander_.get());
    const net::ShardSearchRequest req{q, 20, g.doc_count, g.total_length,
                                      df_for(g, query::required_terms(plan))};
    for (const auto& shard : shards_) {
      const auto resp = std::get<net::ShardSearchResponse>(shard->handle(req));
      for (const auto& hit : resp.hits) {
        ASSERT_TRUE(reference.contains(hit.doc)) << q;
        EXPECT_EQ(hit.score, reference.at(hit.doc)) << q << " doc " << hit.doc;  // exact
        EXPECT_EQ(hit.key, (*corpus_)[hit.doc].key);  // the global id maps to the right doc
        ++checked;
      }
    }
  }
  EXPECT_GT(checked, 500U);
}

TEST_F(ShardedScoresTest, LocalStatsWouldNotMatch) {
  // The problem global stats solve: with each shard's own N/avgdl/df, scores drift.
  const query::Searcher single(*single_, {}, expander_.get());
  std::map<DocId, double> reference;
  for (const auto& h : single.search("late winner at anfield", 1000).hits) {
    reference[h.doc] = h.score;
  }
  const auto resp = std::get<net::ShardSearchResponse>(
      shards_[0]->handle(net::ShardSearchRequest{"late winner at anfield", 5, 0, 0, {}}));
  std::size_t differing = 0;
  for (const auto& h : resp.hits) {
    differing += h.score != reference.at(h.doc) ? 1U : 0U;
  }
  EXPECT_EQ(differing, resp.hits.size());
}

}  // namespace
}  // namespace gaffer::server
