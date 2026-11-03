#include "net/messages.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.hpp"
#include "net/framing.hpp"
#include "net/socket.hpp"

namespace gaffer::net {
namespace {

using namespace std::chrono_literals;
using Bytes = std::vector<std::uint8_t>;

std::vector<Message> samples() {
  const Hit h1{42, 8.358123456789, 1, "match-x", "Arsenal 2-0 Everton", "a **late** **winner**"};
  const Hit h2{7, std::numeric_limits<double>::denorm_min(), 0, "", "", ""};
  return {
      StatsRequest{},
      StatsResponse{2, 4, 3431, 163021, 3, {{"arsenal", 23}, {"ødegaard", 1}, {"", 0}}},
      ShardSearchRequest{"gunners \"late winner\"", 10, 13724, 652085, {{"late", 104}}},
      ShardSearchResponse{3, 118, {h1, h2}},
      SearchRequest{"palmer hat-trick", 5},
      SearchResponse{118, 4, {1, 3}, {h1}, 0.734},
      SearchResponse{},
      ErrorMessage{"shard overloaded"},
  };
}

TEST(MessagesTest, EveryMessageRoundTripsExactly) {
  for (const Message& m : samples()) {
    const Bytes b = encode(m);
    EXPECT_EQ(b[0], kProtocolVersion);
    EXPECT_EQ(decode(b), m) << "type " << static_cast<int>(b[1]);
  }
}

TEST(MessagesTest, ScoresAreBitExact) {
  const double score = 0.1 + 0.2;  // not representable "nicely"; must survive unchanged
  const auto m = std::get<ShardSearchResponse>(
      decode(encode(ShardSearchResponse{0, 1, {{1, score, 0, "k", "t", "s"}}})));
  EXPECT_EQ(m.hits[0].score, score);  // ==, not NEAR
}

TEST(MessagesTest, PartialFlag) {
  EXPECT_FALSE(SearchResponse{}.partial());
  EXPECT_TRUE((SearchResponse{0, 2, {1}, {}, 0}.partial()));
}

TEST(MessagesTest, EveryTruncationIsRejected) {
  for (const Message& m : samples()) {
    const Bytes b = encode(m);
    for (std::size_t len = 0; len < b.size(); ++len) {
      EXPECT_THROW((void)decode(std::span(b).first(len)), FormatError)
          << "type " << static_cast<int>(b[1]) << " len " << len;
    }
  }
}

TEST(MessagesTest, RejectsBadHeaderTrailingBytesAndInflatedCounts) {
  Bytes b = encode(SearchRequest{"q", 1});
  Bytes wrong_version = b;
  wrong_version[0] = 2;
  EXPECT_THROW((void)decode(wrong_version), FormatError);
  Bytes unknown_type = b;
  unknown_type[1] = 99;
  EXPECT_THROW((void)decode(unknown_type), FormatError);
  Bytes trailing = b;
  trailing.push_back(0);
  EXPECT_THROW((void)decode(trailing), FormatError);
  // A df list claiming 2^32 entries in a 10-byte message: rejected before allocating.
  const Bytes inflated{kProtocolVersion, 2, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
  EXPECT_THROW((void)decode(inflated), FormatError);
}

// Fuzz-style: random bytes must decode or throw FormatError, never crash
// (ASan/UBSan check the "never crash" part).
TEST(MessagesTest, RandomBytesNeverCrash) {
  std::mt19937 rng(15440);
  for (int i = 0; i < 20000; ++i) {
    Bytes b(rng() % 64);
    for (auto& x : b) {
      x = static_cast<std::uint8_t>(rng());
    }
    if (b.size() >= 2 && rng() % 2 == 0) {
      b[0] = kProtocolVersion;  // get past the version check half the time
      b[1] = static_cast<std::uint8_t>(1 + rng() % 7);
    }
    try {
      (void)decode(b);
    } catch (const FormatError&) {
    }
  }
}

// --- framing over real sockets ---------------------------------------------

std::pair<Fd, Fd> connected_pair() {
  const Fd listener = listen_tcp("127.0.0.1", 0);
  Fd c = connect_tcp("127.0.0.1", local_port(listener), Deadline::after(1s));
  Fd s = accept_until(listener, Deadline::after(1s));
  return {std::move(c), std::move(s)};
}

TEST(FramingTest, MessageRoundTripOverTcp) {
  auto [c, s] = connected_pair();
  const Message m = SearchRequest{"late winner at anfield", 3};
  ASSERT_EQ(write_frame(c, encode(m), Deadline::after(1s)), IoStatus::kOk);
  const FrameResult f = read_frame(s, Deadline::after(1s));
  ASSERT_EQ(f.status, IoStatus::kOk);
  EXPECT_EQ(decode(f.payload), m);
}

TEST(FramingTest, TwoFramesInOneWriteAreSeparated) {
  auto [c, s] = connected_pair();
  Bytes glued;
  for (const Bytes& p : {Bytes{1, 2, 3}, Bytes{9}}) {
    const auto n = static_cast<std::uint32_t>(p.size());
    glued.insert(glued.end(), {static_cast<std::uint8_t>(n), 0, 0, 0});
    glued.insert(glued.end(), p.begin(), p.end());
  }
  ASSERT_EQ(write_all(c, glued, Deadline::after(1s)), IoStatus::kOk);
  EXPECT_EQ(read_frame(s, Deadline::after(1s)).payload, (Bytes{1, 2, 3}));
  EXPECT_EQ(read_frame(s, Deadline::after(1s)).payload, (Bytes{9}));
}

TEST(FramingTest, ZeroLengthFrame) {
  auto [c, s] = connected_pair();
  ASSERT_EQ(write_frame(c, {}, Deadline::after(1s)), IoStatus::kOk);
  const auto f = read_frame(s, Deadline::after(1s));
  EXPECT_EQ(f.status, IoStatus::kOk);
  EXPECT_TRUE(f.payload.empty());
}

TEST(FramingTest, OversizedLengthIsRejectedWithoutReadingTheBody) {
  auto [c, s] = connected_pair();
  const Bytes header{0xFF, 0xFF, 0xFF, 0xFF};  // claims 4 GiB
  ASSERT_EQ(write_all(c, header, Deadline::after(1s)), IoStatus::kOk);
  const auto f = read_frame(s, Deadline::after(1s));
  EXPECT_EQ(f.status, IoStatus::kError);
  EXPECT_TRUE(f.too_large);
  EXPECT_TRUE(f.payload.empty());  // nothing allocated
}

TEST(FramingTest, TruncatedFrameThenCloseIsClosed) {
  auto [c, s] = connected_pair();
  const Bytes partial{10, 0, 0, 0, 1, 2};  // promises 10 bytes, sends 2, hangs up
  ASSERT_EQ(write_all(c, partial, Deadline::after(1s)), IoStatus::kOk);
  c.reset();
  EXPECT_EQ(read_frame(s, Deadline::after(1s)).status, IoStatus::kClosed);
}

TEST(FramingTest, WriterRefusesOversizedPayload) {
  auto [c, s] = connected_pair();
  const Bytes big(kMaxFrameBytes + 1);
  EXPECT_EQ(write_frame(c, big, Deadline::after(1s)), IoStatus::kError);
}

}  // namespace
}  // namespace gaffer::net
