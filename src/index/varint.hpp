#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gaffer::index {

// Unsigned LEB128 ("varint"): 7 value bits per byte, least significant group
// first. The high bit (0x80) means "another byte follows".
//
//   0        -> 00
//   127      -> 7F
//   128      -> 80 01
//   300      -> AC 02          (300 = 0b10_0101100: low 7 bits 0x2C | 0x80, then 0x02)
//   2^32 - 1 -> FF FF FF FF 0F
//
// Why: postings are mostly small numbers once delta-encoded (gaps between doc
// ids, between positions; small tfs). A fixed uint32 spends 4 bytes on each;
// a varint spends 1 byte on anything below 128.
// Trade-off: decoding branches per byte (slower than fixed-width loads, and
// not SIMD-friendly). Block codecs such as PForDelta exist for that; varint
// is the simple, explainable baseline.

inline constexpr std::size_t kMaxVarint32Bytes = 5;   // ceil(32 / 7)
inline constexpr std::size_t kMaxVarint64Bytes = 10;  // ceil(64 / 7)

// Appends the encoding of `value` to `out`.
void put_varint(std::vector<std::uint8_t>& out, std::uint64_t value);

// Number of bytes put_varint would write.
[[nodiscard]] std::size_t varint_size(std::uint64_t value) noexcept;

// Bounds-checked decoder over a byte buffer, e.g. one read from an index file.
// Every read returns std::nullopt instead of reading past the end or
// accepting a malformed value. On failure the reader's position is unspecified
// and the caller should treat the whole buffer as corrupt.
//
// Rejected input:
//  * truncated: the buffer ends while the continuation bit is set
//  * too long: more than 5 (u32) / 10 (u64) bytes
//  * overflow: bits beyond the target width in the last byte
//  * non-canonical: a trailing 0x00 group (e.g. 80 00 for 0). Every value has
//    exactly one valid encoding, so a byte-for-byte file comparison means the
//    same thing as a value-for-value one.
class VarintReader {
 public:
  explicit VarintReader(std::span<const std::uint8_t> data) noexcept : data_(data) {}

  [[nodiscard]] std::optional<std::uint32_t> read_u32() noexcept;
  [[nodiscard]] std::optional<std::uint64_t> read_u64() noexcept;

  [[nodiscard]] bool at_end() const noexcept { return pos_ == data_.size(); }
  [[nodiscard]] std::size_t position() const noexcept { return pos_; }

 private:
  std::optional<std::uint64_t> read(std::size_t max_bytes, unsigned value_bits) noexcept;

  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

}  // namespace gaffer::index
