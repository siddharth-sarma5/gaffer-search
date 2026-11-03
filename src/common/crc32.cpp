#include "common/crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gaffer {

namespace {

constexpr std::uint32_t kPolynomial = 0xEDB88320U;  // 0x04C11DB7 bit-reversed

// 256-entry lookup table, computed at compile time: entry b is the CRC of the
// single byte b. This lets us process a byte per step instead of a bit per step.
constexpr std::array<std::uint32_t, 256> make_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < table.size(); ++i) {
    std::uint32_t c = i;
    for (int bit = 0; bit < 8; ++bit) {
      c = (c & 1U) != 0 ? (c >> 1U) ^ kPolynomial : c >> 1U;
    }
    table[i] = c;
  }
  return table;
}

constexpr std::array<std::uint32_t, 256> kTable = make_table();

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t previous) noexcept {
  std::uint32_t c = previous ^ 0xFFFFFFFFU;
  for (const std::uint8_t byte : data) {
    c = kTable[(c ^ byte) & 0xFFU] ^ (c >> 8U);
  }
  return c ^ 0xFFFFFFFFU;
}

}  // namespace gaffer
