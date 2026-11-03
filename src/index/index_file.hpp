#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "index/inverted_index.hpp"

namespace gaffer::index {

// On-disk index format, version 1. Fixed-width integers are little-endian.
//
//   offset  size
//   0       8      magic "GAFFERIX"
//   8       4      u32 format version (= 1)
//   12      4      u32 flags: bit 0 = stemming on, bit 1 = stopwords removed
//   16      8      u64 corpus fingerprint (corpus::fingerprint of the source corpus)
//   24      ...    body, all varints:
//                    doc_count, total_length, doc_count x doc_length,
//                    term_count, then per term in ascending byte order:
//                      term_length, term bytes, postings_length, postings bytes
//                      (postings bytes = encode_posting_list, delta + varint)
//   end-4   4      u32 CRC-32 of every preceding byte
//
// Design notes:
//  * Magic + version: a loader can say "not an index file" or "written by a
//    newer version" instead of misreading the bytes.
//  * Flags: the analyzer settings travel with the index, because queries must
//    be analysed the same way the documents were.
//  * Fingerprint: an index is useless without its corpus (titles, snippets),
//    so loading checks that they belong together.
//  * Each term's postings are length-prefixed. That bounds every read, and a
//    future version could skip terms without decoding them (or mmap and
//    decode lazily).
//  * Sorted terms make the file byte-for-byte deterministic.
//  * The CRC goes last so the file can be written in one streaming pass.
inline constexpr std::array<char, 8> kIndexMagic{'G', 'A', 'F', 'F', 'E', 'R', 'I', 'X'};
inline constexpr std::uint32_t kIndexFormatVersion = 1;
inline constexpr std::uint32_t kFlagStem = 1U << 0U;
inline constexpr std::uint32_t kFlagStopwords = 1U << 1U;
inline constexpr std::size_t kIndexHeaderBytes = 24;
inline constexpr std::size_t kIndexFooterBytes = 4;

// Sizes of a serialised index, for `gaffer index` and the benchmarks.
struct IndexFileStats {
  std::size_t file_bytes = 0;                  // the whole file
  std::size_t postings_bytes = 0;              // compressed postings only
  std::size_t fixed_width_postings_bytes = 0;  // the same postings as plain uint32s
};

// Serialises `index` into the v1 format.
[[nodiscard]] std::vector<std::uint8_t> serialize_index(const InvertedIndex& index,
                                                        std::uint64_t corpus_fingerprint,
                                                        IndexFileStats* stats = nullptr);

// Writes the index to `path` atomically: the bytes go to "<path>.tmp", which is
// then renamed over `path`, so readers never see a half-written file.
// Throws gaffer::IoError on failure.
IndexFileStats write_index_file(const InvertedIndex& index, std::uint64_t corpus_fingerprint,
                                const std::filesystem::path& path);

struct LoadedIndex {
  InvertedIndex index;
  std::uint64_t corpus_fingerprint = 0;
};

// Parses and fully validates a v1 index file held in memory. Throws
// gaffer::FormatError with a reason on any problem: too small, wrong magic,
// unsupported version, unknown flags, checksum mismatch, or (even when the
// checksum is valid) a structurally invalid body: unsorted or duplicate terms,
// undecodable postings, doc lengths not summing to the stored total, or
// trailing bytes. Never reads out of bounds.
[[nodiscard]] LoadedIndex deserialize_index(std::span<const std::uint8_t> bytes);

// Reads and deserialises an index file. Throws gaffer::IoError if it can't be
// read, gaffer::FormatError if it's invalid.
[[nodiscard]] LoadedIndex load_index_file(const std::filesystem::path& path);

}  // namespace gaffer::index
