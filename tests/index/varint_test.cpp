#include "index/varint.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include <gtest/gtest.h>

namespace gaffer::index {
namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes encode(std::uint64_t v) {
  Bytes out;
  put_varint(out, v);
  return out;
}

TEST(VarintTest, KnownEncodings) {
  EXPECT_EQ(encode(0), (Bytes{0x00}));
  EXPECT_EQ(encode(1), (Bytes{0x01}));
  EXPECT_EQ(encode(127), (Bytes{0x7F}));
  EXPECT_EQ(encode(128), (Bytes{0x80, 0x01}));
  EXPECT_EQ(encode(300), (Bytes{0xAC, 0x02}));
  EXPECT_EQ(encode(16383), (Bytes{0xFF, 0x7F}));
  EXPECT_EQ(encode(16384), (Bytes{0x80, 0x80, 0x01}));
  EXPECT_EQ(encode(std::numeric_limits<std::uint32_t>::max()),
            (Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0x0F}));
  EXPECT_EQ(encode(std::numeric_limits<std::uint64_t>::max()).size(), kMaxVarint64Bytes);
}

TEST(VarintTest, SizeMatchesEncoding) {
  for (const std::uint64_t v : {0ULL, 127ULL, 128ULL, 16383ULL, 16384ULL, 2097151ULL, 2097152ULL,
                                0xFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL}) {
    EXPECT_EQ(varint_size(v), encode(v).size()) << v;
  }
}

TEST(VarintTest, RoundTripsBoundariesAsSequence) {
  const std::vector<std::uint32_t> values{
      0,     1,       127,     128,       255,       16383,
      16384, 2097151, 2097152, 268435455, 268435456, std::numeric_limits<std::uint32_t>::max()};
  Bytes buf;
  for (const auto v : values) {
    put_varint(buf, v);
  }
  VarintReader r(buf);
  for (const auto v : values) {
    EXPECT_EQ(r.read_u32(), v);
  }
  EXPECT_TRUE(r.at_end());
  EXPECT_FALSE(r.read_u32().has_value());  // reading past the end fails cleanly
}

// Property test: random values of every magnitude round-trip.
TEST(VarintTest, RandomRoundTrip) {
  std::mt19937_64 rng(440868);
  Bytes buf;
  std::vector<std::uint64_t> values;
  for (int i = 0; i < 10000; ++i) {
    const unsigned bits = static_cast<unsigned>(rng() % 65);  // 0..64 significant bits
    const std::uint64_t v = bits == 0 ? 0 : (rng() >> (64 - bits));
    values.push_back(v);
    put_varint(buf, v);
  }
  VarintReader r(buf);
  for (const auto v : values) {
    ASSERT_EQ(r.read_u64(), v);
  }
  EXPECT_TRUE(r.at_end());
}

std::optional<std::uint32_t> decode32(const Bytes& b) {
  VarintReader r(b);
  return r.read_u32();
}

std::optional<std::uint64_t> decode64(const Bytes& b) {
  VarintReader r(b);
  return r.read_u64();
}

TEST(VarintTest, RejectsTruncatedInput) {
  EXPECT_FALSE(decode32({}).has_value());
  EXPECT_FALSE(decode32({0x80}).has_value());
  EXPECT_FALSE(decode32({0xFF, 0xFF}).has_value());
}

TEST(VarintTest, RejectsTooLong) {
  EXPECT_FALSE(decode32({0x80, 0x80, 0x80, 0x80, 0x80, 0x01}).has_value());
  EXPECT_FALSE(decode64(Bytes(11, 0x80)).has_value());
}

TEST(VarintTest, RejectsOverflow) {
  // 5th byte may carry only 4 bits for u32: 0x10 would be bit 32.
  EXPECT_FALSE(decode32({0xFF, 0xFF, 0xFF, 0xFF, 0x10}).has_value());
  EXPECT_EQ(decode32({0xFF, 0xFF, 0xFF, 0xFF, 0x0F}), 0xFFFFFFFFU);
  // ...but the same bytes are a valid u64.
  EXPECT_EQ(decode64({0xFF, 0xFF, 0xFF, 0xFF, 0x10}), 0x10FFFFFFFULL);  // 28 ones + bit 32
  // 10th byte may carry only 1 bit for u64.
  Bytes too_big(9, 0xFF);
  too_big.push_back(0x02);
  EXPECT_FALSE(decode64(too_big).has_value());
}

TEST(VarintTest, RejectsNonCanonicalPadding) {
  EXPECT_FALSE(decode32({0x80, 0x00}).has_value());        // 0 padded to 2 bytes
  EXPECT_FALSE(decode32({0x81, 0x80, 0x00}).has_value());  // 1 padded to 3 bytes
  EXPECT_EQ(decode32({0x00}), 0U);                         // the canonical zero is fine
}

TEST(VarintTest, SmallValuesTakeOneByte) {
  // The point of varints for postings: gaps under 128 cost 1 byte, not 4.
  for (std::uint64_t v = 0; v < 128; ++v) {
    EXPECT_EQ(varint_size(v), 1U);
  }
}

}  // namespace
}  // namespace gaffer::index
