#include "index/index_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/crc32.hpp"
#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "corpus/document.hpp"
#include "index/inverted_index.hpp"
#include "text/analyzer.hpp"

namespace gaffer::index {
namespace {

InvertedIndex small_index(text::AnalyzerOptions opts = {}) {
  IndexBuilder b{text::Analyzer{opts}};
  b.add("Salah scores a late winner");
  b.add("Haaland scores twice");
  return std::move(b).build();
}

std::uint32_t read_u32_le(std::span<const std::uint8_t> b, std::size_t at) {
  return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8U) |
         (static_cast<std::uint32_t>(b[at + 2]) << 16U) |
         (static_cast<std::uint32_t>(b[at + 3]) << 24U);
}

TEST(SerializeIndexTest, HeaderFields) {
  const auto bytes = serialize_index(small_index(), 0x1122334455667788ULL);
  ASSERT_GT(bytes.size(), kIndexHeaderBytes + kIndexFooterBytes);
  EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 8), "GAFFERIX");
  EXPECT_EQ(read_u32_le(bytes, 8), kIndexFormatVersion);
  EXPECT_EQ(read_u32_le(bytes, 12), kFlagStem | kFlagStopwords);
  EXPECT_EQ(bytes[16], 0x88);  // fingerprint, little-endian
  EXPECT_EQ(bytes[23], 0x11);
}

TEST(SerializeIndexTest, FlagsFollowAnalyzerOptions) {
  const auto bytes = serialize_index(small_index({.remove_stopwords = false, .stem = false}), 0);
  EXPECT_EQ(read_u32_le(bytes, 12), 0U);
}

TEST(SerializeIndexTest, FooterIsCrcOfEverythingBefore) {
  const auto bytes = serialize_index(small_index(), 7);
  const std::span<const std::uint8_t> all(bytes);
  const auto body = all.first(bytes.size() - kIndexFooterBytes);
  EXPECT_EQ(read_u32_le(all, bytes.size() - 4), crc32(body));
}

TEST(SerializeIndexTest, IsDeterministicAndReportsSizes) {
  IndexFileStats a;
  IndexFileStats b;
  const auto first = serialize_index(small_index(), 1, &a);
  const auto second = serialize_index(small_index(), 1, &b);
  EXPECT_EQ(first, second);
  EXPECT_EQ(a.file_bytes, first.size());
  EXPECT_GT(a.postings_bytes, 0U);
  EXPECT_GT(a.fixed_width_postings_bytes, a.postings_bytes);
}

TEST(WriteIndexFileTest, WritesExactlyTheSerialisedBytesAndNoTempFile) {
  const auto dir = std::filesystem::temp_directory_path() / "gaffer_index_file_test";
  std::filesystem::create_directories(dir);
  const auto path = dir / "small.gaffer";
  const InvertedIndex idx = small_index();
  const IndexFileStats stats = write_index_file(idx, 42, path);

  std::vector<std::uint8_t> on_disk(std::filesystem::file_size(path));
  std::ifstream in(path, std::ios::binary);
  in.read(reinterpret_cast<char*>(on_disk.data()),  // NOLINT(*-reinterpret-cast)
          static_cast<std::streamsize>(on_disk.size()));
  EXPECT_EQ(on_disk, serialize_index(idx, 42));
  EXPECT_EQ(stats.file_bytes, on_disk.size());
  EXPECT_FALSE(std::filesystem::exists(dir / "small.gaffer.tmp"));
  std::filesystem::remove_all(dir);
}

TEST(WriteIndexFileTest, UnwritableLocationThrowsIoError) {
  EXPECT_THROW((void)write_index_file(small_index(), 0, "/nonexistent-dir/x.gaffer"), IoError);
}

// --- loading -------------------------------------------------------------

void expect_same_index(const InvertedIndex& a, const InvertedIndex& b) {
  ASSERT_EQ(a.stats().doc_count, b.stats().doc_count);
  EXPECT_EQ(a.stats().total_length, b.stats().total_length);
  EXPECT_EQ(a.term_count(), b.term_count());
  EXPECT_EQ(a.posting_count(), b.posting_count());
  EXPECT_EQ(a.position_count(), b.position_count());
  EXPECT_EQ(a.analyzer_options().stem, b.analyzer_options().stem);
  EXPECT_EQ(a.analyzer_options().remove_stopwords, b.analyzer_options().remove_stopwords);
  for (DocId d = 0; d < a.stats().doc_count; ++d) {
    ASSERT_EQ(a.doc_length(d), b.doc_length(d));
  }
  EXPECT_EQ(a.sorted_terms(), b.sorted_terms());
  for (const auto term : a.sorted_terms()) {
    const PostingList& la = a.posting_list(term);
    const PostingList& lb = b.posting_list(term);
    ASSERT_EQ(la.size(), lb.size()) << term;
    for (std::size_t i = 0; i < la.size(); ++i) {
      ASSERT_EQ(la.postings()[i], lb.postings()[i]) << term;
      const auto pa = la.positions(i);
      const auto pb = lb.positions(i);
      ASSERT_TRUE(std::equal(pa.begin(), pa.end(), pb.begin(), pb.end())) << term;
    }
  }
}

TEST(DeserializeIndexTest, RoundTripsSmallIndexAndOptions) {
  for (const auto opts : {text::AnalyzerOptions{}, text::AnalyzerOptions{false, false}}) {
    const InvertedIndex idx = small_index(opts);
    const LoadedIndex loaded = deserialize_index(serialize_index(idx, 99));
    EXPECT_EQ(loaded.corpus_fingerprint, 99U);
    expect_same_index(idx, loaded.index);
  }
}

TEST(DeserializeIndexTest, RoundTripsTheRealCorpusIndex) {
  const corpus::Corpus c = corpus::load_tsv_file("data/corpus/pl-2025-26.tsv");
  const InvertedIndex idx = build_index(c, text::Analyzer{});
  const auto bytes = serialize_index(idx, corpus::fingerprint(c));
  const LoadedIndex loaded = deserialize_index(bytes);
  EXPECT_EQ(loaded.corpus_fingerprint, corpus::fingerprint(c));
  expect_same_index(idx, loaded.index);
  // Re-serialising the loaded index reproduces the file byte for byte.
  EXPECT_EQ(serialize_index(loaded.index, loaded.corpus_fingerprint), bytes);
}

void expect_format_error(std::span<const std::uint8_t> bytes, const std::string& needle) {
  try {
    (void)deserialize_index(bytes);
    FAIL() << "expected FormatError containing '" << needle << "'";
  } catch (const FormatError& e) {
    EXPECT_NE(std::string(e.what()).find(needle), std::string::npos) << e.what();
  }
}

// Recomputes the footer so a deliberately broken body passes the checksum and
// has to be caught by the structural checks.
std::vector<std::uint8_t> with_valid_crc(std::vector<std::uint8_t> bytes) {
  bytes.resize(bytes.size() - kIndexFooterBytes);
  const std::uint32_t c = crc32(bytes);
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>(c >> shift));
  }
  return bytes;
}

TEST(DeserializeIndexTest, RejectsBadHeaders) {
  const auto good = serialize_index(small_index(), 1);
  expect_format_error(std::span(good).first(10), "too small");
  auto bad_magic = good;
  bad_magic[0] = 'X';
  expect_format_error(bad_magic, "bad magic");
  auto future = good;
  future[8] = 2;
  expect_format_error(future, "unsupported index format version 2 (this build reads 1)");
  auto flags = with_valid_crc([&] {
    auto b = good;
    b[12] |= 0x80;
    return b;
  }());
  expect_format_error(flags, "unknown flags");
}

TEST(DeserializeIndexTest, ChecksumCatchesAnyBodyByteFlip) {
  const auto good = serialize_index(small_index(), 1);
  for (std::size_t i = kIndexHeaderBytes; i < good.size(); ++i) {
    auto bad = good;
    bad[i] ^= 0x01;
    EXPECT_THROW((void)deserialize_index(bad), FormatError) << "byte " << i;
  }
}

// Fuzz-style: every truncation must fail cleanly. Under ASan this also proves
// no out-of-bounds read happens on the way to the error.
TEST(DeserializeIndexTest, EveryTruncationIsRejected) {
  const auto good = serialize_index(small_index(), 1);
  for (std::size_t len = 0; len < good.size(); ++len) {
    EXPECT_THROW((void)deserialize_index(std::span(good).first(len)), FormatError) << len;
  }
}

// Valid checksum but invalid structure: the body validation must catch these.
TEST(DeserializeIndexTest, StructuralChecksBehindAValidChecksum) {
  // Hand-assembled bodies (after the 24-byte header).
  // Built with push_back/resize: GCC 13 -O3 reports a false -Wstringop-overflow
  // for vector::insert after inlining here.
  auto make = [](const std::vector<std::uint8_t>& body) {
    std::vector<std::uint8_t> b;
    b.reserve(kIndexHeaderBytes + body.size() + kIndexFooterBytes);
    for (const char c : kIndexMagic) {
      b.push_back(static_cast<std::uint8_t>(c));
    }
    // version 1, flags 3 (stem + stopwords), fingerprint 0
    for (const int x : {1, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}) {
      b.push_back(static_cast<std::uint8_t>(x));
    }
    for (const std::uint8_t x : body) {
      b.push_back(x);
    }
    b.resize(b.size() + kIndexFooterBytes, 0);
    return with_valid_crc(b);
  };
  // 1 doc of length 1; 1 term "a" with postings {n=1, doc 0, tf 1, pos 0}.
  const std::vector<std::uint8_t> ok_body{1, 1, 1, 1, 1, 'a', 4, 1, 0, 1, 0};
  EXPECT_NO_THROW((void)deserialize_index(make(ok_body)));

  expect_format_error(make({1, 2, 1, 1, 1, 'a', 4, 1, 0, 1, 0}), "don't add up");
  expect_format_error(make({1, 1, 1, 1, 1, 'a', 4, 1, 5, 1, 0}), "undecodable postings");
  expect_format_error(make({1, 1, 1, 1, 1, 'a', 4, 1, 0, 1, 0, 7}), "trailing bytes");
  expect_format_error(make({1, 1, 1, 2, 1, 'b', 4, 1, 0, 1, 0, 1, 'a', 4, 1, 0, 1, 0}),
                      "not strictly sorted");
  expect_format_error(make({1, 1, 1, 1, 0}), "bad term length");
  expect_format_error(make({1, 1, 1, 1, 1, 'a', 50}), "bad postings length");
  expect_format_error(make({0x80, 0x80, 0x80, 0x80, 0x0F}), "bad document count");
}

TEST(LoadIndexFileTest, MissingFileIsIoError) {
  EXPECT_THROW((void)load_index_file("does/not/exist.gaffer"), IoError);
}

TEST(CorpusFingerprintTest, ChangesWithAnyField) {
  const corpus::Corpus base({{"k", corpus::DocType::kMatch, "Title", "Body"}});
  const auto fp = corpus::fingerprint(base);
  EXPECT_EQ(fp,
            corpus::fingerprint(corpus::Corpus({{"k", corpus::DocType::kMatch, "Title", "Body"}})));
  EXPECT_NE(fp,
            corpus::fingerprint(corpus::Corpus({{"k", corpus::DocType::kClub, "Title", "Body"}})));
  EXPECT_NE(
      fp, corpus::fingerprint(corpus::Corpus({{"k", corpus::DocType::kMatch, "Title", "Body!"}})));
  // Field boundaries matter: ("Tit", "leBody") differs from ("Title", "Body").
  EXPECT_NE(fp,
            corpus::fingerprint(corpus::Corpus({{"k", corpus::DocType::kMatch, "Tit", "leBody"}})));
}

}  // namespace
}  // namespace gaffer::index
