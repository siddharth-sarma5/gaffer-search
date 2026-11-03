#include "index/partition.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "corpus/document.hpp"

namespace gaffer::index {
namespace {

corpus::Corpus numbered(std::uint32_t n) {
  std::vector<corpus::Document> docs;
  for (std::uint32_t i = 0; i < n; ++i) {
    docs.push_back({"d" + std::to_string(i), corpus::DocType::kMatch, "t", "b"});
  }
  return corpus::Corpus(std::move(docs));
}

TEST(PartitionTest, IdMappingRoundTrips) {
  for (const std::uint32_t n : {1U, 2U, 3U, 8U}) {
    for (DocId g = 0; g < 100; ++g) {
      EXPECT_EQ(global_of(local_of(g, n), shard_of(g, n), n), g);
    }
  }
}

TEST(PartitionTest, RoundRobinIsBalancedCompleteAndOrderPreserving) {
  const corpus::Corpus c = numbered(10);
  const auto parts = partition(c, 3);
  ASSERT_EQ(parts.size(), 3U);
  EXPECT_EQ(parts[0].size(), 4U);  // 0 3 6 9
  EXPECT_EQ(parts[1].size(), 3U);  // 1 4 7
  EXPECT_EQ(parts[2].size(), 3U);  // 2 5 8
  for (std::uint32_t s = 0; s < 3; ++s) {
    for (DocId local = 0; local < parts[s].size(); ++local) {
      // Each shard's local doc is exactly the global doc the mapping says.
      EXPECT_EQ(parts[s][local].key, c[global_of(local, s, 3)].key);
    }
  }
}

TEST(PartitionTest, OneShardIsTheWholeCorpusAndZeroIsAnError) {
  const corpus::Corpus c = numbered(5);
  EXPECT_EQ(partition(c, 1)[0].size(), 5U);
  EXPECT_THROW((void)partition(c, 0), Error);
  EXPECT_EQ(partition(c, 8)[7].size(), 0U);  // more shards than docs: some are empty
}

TEST(WriteTsvTest, RoundTripsThroughTheLoaderIncludingEscapes) {
  const corpus::Corpus c({{"k\t1", corpus::DocType::kClub, "A\\B", "line1\nline2\ttab"},
                          {"k2", corpus::DocType::kSeason, "", ""}});
  const auto path = std::filesystem::temp_directory_path() / "gaffer_write_tsv_test.tsv";
  corpus::write_tsv_file(c, path);
  const corpus::Corpus back = corpus::load_tsv_file(path);
  ASSERT_EQ(back.size(), 2U);
  for (DocId i = 0; i < 2; ++i) {
    EXPECT_EQ(back[i].key, c[i].key);
    EXPECT_EQ(back[i].type, c[i].type);
    EXPECT_EQ(back[i].title, c[i].title);
    EXPECT_EQ(back[i].body, c[i].body);
  }
  EXPECT_EQ(corpus::fingerprint(back), corpus::fingerprint(c));
  std::filesystem::remove(path);
}

}  // namespace
}  // namespace gaffer::index
