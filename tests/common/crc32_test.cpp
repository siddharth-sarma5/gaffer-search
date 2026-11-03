#include "common/crc32.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace gaffer {
namespace {

std::vector<std::uint8_t> bytes(std::string_view s) { return {s.begin(), s.end()}; }

TEST(Crc32Test, StandardCheckValues) {
  EXPECT_EQ(crc32(bytes("123456789")), 0xCBF43926U);  // the published check value
  EXPECT_EQ(crc32(bytes("")), 0x00000000U);
  EXPECT_EQ(crc32(bytes("The quick brown fox jumps over the lazy dog")), 0x414FA339U);
}

TEST(Crc32Test, IncrementalEqualsOneShot) {
  const auto all = bytes("late winner at anfield");
  const std::span<const std::uint8_t> s(all);
  EXPECT_EQ(crc32(s.subspan(7), crc32(s.first(7))), crc32(s));
}

TEST(Crc32Test, DetectsSingleBitFlips) {
  auto data = bytes("Ask the Gaffer");
  const std::uint32_t good = crc32(data);
  for (std::size_t i = 0; i < data.size(); ++i) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      data[i] ^= static_cast<std::uint8_t>(1U << bit);
      EXPECT_NE(crc32(data), good) << i << ":" << bit;
      data[i] ^= static_cast<std::uint8_t>(1U << bit);
    }
  }
}

}  // namespace
}  // namespace gaffer
