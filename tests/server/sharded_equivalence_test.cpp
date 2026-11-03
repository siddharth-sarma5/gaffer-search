// The Milestone 3 acceptance property: a sharded cluster returns exactly what
// a single node returns: same documents, same order, bit-identical scores,
// same match counts. Checked over real TCP for N = 1, 2, 3, 4 shards.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cluster.hpp"
#include "corpus/corpus.hpp"
#include "eval/metrics.hpp"
#include "index/inverted_index.hpp"
#include "net/messages.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {
namespace {

std::vector<std::string> all_queries() {
  std::vector<std::string> qs;
  for (const auto& q : eval::load_queries("eval/queries.tsv")) {
    qs.push_back(q.text);
  }
  // The golden-file queries plus phrase, mixed, expansion and edge cases.
  for (const char* q : {"late winner at anfield",
                        "palmer hat-trick",
                        "haaland brace",
                        "gunners",
                        "champions",
                        "relegated",
                        "north london derby",
                        "sent off second yellow",
                        "comeback win",
                        "goalless draw",
                        "own goal",
                        "stoppage time penalty",
                        "odegaard",
                        "stadium of light",
                        "biggest win",
                        "\"late winner\"",
                        "\"north london derby\" arsenal",
                        "\"stadium of light\" \"clean sheet\"",
                        "red devils comeback",
                        "salah salah penalty",
                        "the of at",
                        "mbappe",
                        ""}) {
    qs.emplace_back(q);
  }
  return qs;
}

void expect_equivalent(const corpus::Corpus& corpus, const query::NicknameExpander& expander,
                       std::uint32_t shards, const std::vector<std::string>& queries,
                       std::uint32_t k) {
  const auto idx = index::build_index(corpus, text::Analyzer{});
  const query::Searcher single(idx, {}, &expander);
  const testing::Cluster cluster(corpus, shards, expander);
  for (const auto& q : queries) {
    const auto ref = single.search(q, k);
    const auto got = cluster.broker->search(net::SearchRequest{q, k});
    ASSERT_FALSE(got.partial()) << q;
    EXPECT_EQ(got.matched, ref.matched) << shards << " shards, '" << q << "'";
    ASSERT_EQ(got.hits.size(), ref.hits.size()) << shards << " shards, '" << q << "'";
    for (std::size_t i = 0; i < ref.hits.size(); ++i) {
      EXPECT_EQ(got.hits[i].doc, ref.hits[i].doc) << shards << " shards, '" << q << "' #" << i;
      EXPECT_EQ(got.hits[i].score, ref.hits[i].score) << shards << " shards, '" << q << "' #" << i;
      EXPECT_EQ(got.hits[i].key, corpus[ref.hits[i].doc].key);
    }
  }
}

class ShardedEquivalenceTest : public ::testing::TestWithParam<std::uint32_t> {};

TEST_P(ShardedEquivalenceTest, SameTopKAndExactScoresAsSingleNode) {
  const auto corpus = corpus::load_tsv_file("data/corpus/pl-2025-26.tsv");
  const auto expander = query::NicknameExpander::load_tsv_file("data/nicknames.tsv");
  expect_equivalent(corpus, expander, GetParam(), all_queries(), 10);
}

INSTANTIATE_TEST_SUITE_P(Shards, ShardedEquivalenceTest, ::testing::Values(1U, 2U, 3U, 4U));

// At scale: the 13,724-doc corpus (history + 2025-26), 4 shards, k = 50 to
// exercise deeper merges and the many score ties among results-only docs.
TEST(ShardedEquivalenceAtScaleTest, FullHistoryCorpusFourShards) {
  const std::vector<std::filesystem::path> files{"data/corpus/pl-history.tsv",
                                                 "data/corpus/pl-2025-26.tsv"};
  const auto corpus = corpus::load_tsv_files(files);
  const auto expander = query::NicknameExpander::load_tsv_file("data/nicknames.tsv");
  expect_equivalent(corpus, expander, 4,
                    {"arsenal thrashing", "goalless draw", "\"came from behind\" liverpool",
                     "comeback win", "spurs high-scoring", "manchester united clean sheet"},
                    50);
}

}  // namespace
}  // namespace gaffer::server
