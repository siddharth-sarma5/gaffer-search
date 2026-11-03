#include "index/index_file.hpp"

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
