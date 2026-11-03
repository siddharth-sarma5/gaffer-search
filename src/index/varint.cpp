#include "index/varint.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gaffer::index {

namespace {

constexpr std::uint8_t kContinue = 0x80;
constexpr std::uint8_t kPayload = 0x7F;
constexpr unsigned kBitsPerByte = 7;

}  // namespace

void put_varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
  while (value >= kContinue) {
    out.push_back(static_cast<std::uint8_t>((value & kPayload) | kContinue));
    value >>= kBitsPerByte;
  }
  out.push_back(static_cast<std::uint8_t>(value));
}

std::size_t varint_size(std::uint64_t value) noexcept {
  std::size_t n = 1;
  while (value >= kContinue) {
    value >>= kBitsPerByte;
    ++n;
  }
  return n;
}

std::optional<std::uint32_t> VarintReader::read_u32() noexcept {
  const auto v = read(kMaxVarint32Bytes, 32);
  if (!v) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(*v);
}

std::optional<std::uint64_t> VarintReader::read_u64() noexcept {
  return read(kMaxVarint64Bytes, 64);
}

std::optional<std::uint64_t> VarintReader::read(std::size_t max_bytes,
                                                unsigned value_bits) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < max_bytes; ++i) {
    if (pos_ >= data_.size()) {
      return std::nullopt;  // truncated
    }
    const std::uint8_t byte = data_[pos_++];
    const std::uint64_t payload = byte & kPayload;
    const auto shift = static_cast<unsigned>(i * kBitsPerByte);

    // The last allowed byte may only carry the bits that still fit:
    // 32 - 28 = 4 bits for u32, 64 - 63 = 1 bit for u64.
    if (i + 1 == max_bytes && (payload >> (value_bits - shift)) != 0) {
      return std::nullopt;  // overflow
    }
    value |= payload << shift;

    if ((byte & kContinue) == 0) {
      if (byte == 0 && i > 0) {
        return std::nullopt;  // non-canonical: padded with a zero group
      }
      return value;
    }
  }
  return std::nullopt;  // continuation bit still set after max_bytes: too long
}

}  // namespace gaffer::index
