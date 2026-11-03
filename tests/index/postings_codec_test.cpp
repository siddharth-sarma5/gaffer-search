#include "index/postings_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "index/varint.hpp"
#include "text/analyzer.hpp"

namespace gaffer::index {
namespace {

using Bytes = std::vector<std::uint8_t>;

PostingList make_list(const std::vector<std::pair<DocId, std::vector<std::uint32_t>>>& entries) {
  PostingList list;
  for (const auto& [doc, pos] : entries) {
    list.append(doc, pos);
  }
  return list;
}

bool same(const PostingList& a, const PostingList& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!(a.postings()[i] == b.postings()[i])) {
      return false;
    }
    const auto pa = a.positions(i);
    const auto pb = b.positions(i);
    if (!std::equal(pa.begin(), pa.end(), pb.begin(), pb.end())) {
      return false;
    }
  }
  return true;
}

TEST(PostingsCodecTest, KnownLayout) {
  // docs 3 and 41; positions {5, 9} and {130}.
  const PostingList list = make_list({{3, {5, 9}}, {41, {130}}});
  // n=2 | doc 3, tf 2, pos 5, gap 4 | doc gap 38, tf 1, pos 130 (two bytes: 0x82 0x01)
  EXPECT_EQ(encode_posting_list(list), (Bytes{2, 3, 2, 5, 4, 38, 1, 0x82, 0x01}));
  EXPECT_EQ(fixed_width_size(list), 2U * 12U + 3U * 4U);
}

TEST(PostingsCodecTest, RoundTripsIncludingEmptyAndLargeValues) {
  for (const PostingList& list : {PostingList{}, make_list({{0, {0}}}),
                                  make_list({{0, {0, 1, 2}}, {1, {7}}, {999999, {4000000000U}}})}) {
    const auto decoded = decode_posting_list(encode_posting_list(list), 1000000);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(same(list, *decoded));
  }
}

TEST(PostingsCodecTest, RejectsMalformedInput) {
  const DocId n = 100;
  const std::vector<Bytes> bad{
      {},                                       // no count
      {1},                                      // count but no posting
      {1, 5, 1},                                // posting without its position
      {2, 3, 1, 0, 0, 1, 0},                    // second doc gap is 0 (duplicate doc)
      {1, 3, 2, 5, 0},                          // second position gap is 0
      {1, 3, 0},                                // tf = 0
      {1, 100, 1, 0},                           // doc id == doc_count
      {1, 3, 1, 0, 0xFF},                       // trailing byte
      {0x80, 0x80, 0x80, 0x80, 0x0F, 3, 1, 0},  // implausibly large count
  };
  for (const auto& b : bad) {
    EXPECT_FALSE(decode_posting_list(b, n).has_value()) << testing::PrintToString(b);
  }
}

TEST(PostingsCodecTest, EveryTruncationIsRejected) {
  const Bytes good = encode_posting_list(make_list({{3, {5, 9}}, {41, {130, 131}}, {77, {2}}}));
  ASSERT_TRUE(decode_posting_list(good, 100).has_value());
  for (std::size_t len = 0; len < good.size(); ++len) {
    EXPECT_FALSE(decode_posting_list(std::span(good).first(len), 100).has_value()) << len;
  }
}

// Every term of the real index round-trips, and we record the compression ratio.
TEST(PostingsCodecTest, RealCorpusRoundTripAndRatio) {
  const corpus::Corpus c = corpus::load_tsv_file("data/corpus/pl-2025-26.tsv");
  const InvertedIndex idx = build_index(c, text::Analyzer{});
  std::size_t compressed = 0;
  std::size_t fixed = 0;
  for (const auto term : idx.sorted_terms()) {
    const PostingList& list = idx.posting_list(term);
    const Bytes bytes = encode_posting_list(list);
    compressed += bytes.size();
    fixed += fixed_width_size(list);
    const auto decoded = decode_posting_list(bytes, idx.stats().doc_count);
    ASSERT_TRUE(decoded.has_value()) << term;
    ASSERT_TRUE(same(list, *decoded)) << term;
  }
  EXPECT_LT(compressed, fixed / 2);  // deltas + varints should at least halve it
  std::cout << "[postings] fixed-width " << fixed << " B, compressed " << compressed << " B ("
            << (100.0 * static_cast<double>(compressed) / static_cast<double>(fixed)) << "%)\n";
}

}  // namespace
}  // namespace gaffer::index
