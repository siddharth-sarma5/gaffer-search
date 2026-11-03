#include "server/shard_server.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "net/framing.hpp"
#include "net/messages.hpp"
#include "net/rpc.hpp"
#include "net/socket.hpp"
#include "query/nicknames.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {
namespace {

using namespace std::chrono_literals;

std::unique_ptr<ShardServer> start_server(ShardServerOptions opts = {}) {
  auto corpus = corpus::load_tsv_file("tests/fixtures/mini_corpus.tsv");
  auto index = index::build_index(corpus, text::Analyzer{});
  auto s = std::make_unique<ShardServer>(std::move(corpus), std::move(index),
                                         query::NicknameExpander({{"toffees", "Everton"}}), opts);
  s->start();
  return s;
}

TEST(ShardServerTest, AnswersSearchOverTcp) {
  const auto server = start_server();
  const auto reply = net::call("127.0.0.1", server->port(),
                               net::ShardSearchRequest{"salah late winner", 2, 0, 0, {}},
                               net::Deadline::after(2s));
  ASSERT_TRUE(reply.has_value());
  const auto& r = std::get<net::ShardSearchResponse>(*reply);
  ASSERT_FALSE(r.hits.empty());
  EXPECT_EQ(r.hits[0].key, "m-anfield");
  EXPECT_NE(r.hits[0].snippet.find("**late** **winner**"), std::string::npos);
  EXPECT_EQ(server->requests_served(), 1U);
}

TEST(ShardServerTest, GlobalDocIdsFollowThePartitionMapping) {
  const auto server = start_server({.shard_id = 2, .num_shards = 3});
  const auto reply =
      net::call("127.0.0.1", server->port(), net::ShardSearchRequest{"salah", 10, 0, 0, {}},
                net::Deadline::after(2s));
  ASSERT_TRUE(reply.has_value());
  for (const auto& h : std::get<net::ShardSearchResponse>(*reply).hits) {
    EXPECT_EQ(h.doc % 3, 2U);  // global = local * 3 + 2
    EXPECT_EQ(h.shard, 2U);
  }
}

TEST(ShardServerTest, ManyRequestsOnOneKeepAliveConnection) {
  const auto server = start_server();
  const net::Fd fd = net::connect_tcp("127.0.0.1", server->port(), net::Deadline::after(1s));
  for (int i = 0; i < 20; ++i) {
    const auto reply = net::exchange(fd, net::ShardSearchRequest{"toffees", 3, 0, 0, {}},
                                     net::Deadline::after(2s));
    ASSERT_TRUE(reply.has_value()) << i;
  }
  EXPECT_EQ(server->requests_served(), 20U);
}

TEST(ShardServerTest, GarbageGetsAnErrorAndTheServerKeepsServing) {
  const auto server = start_server();
  {
    const net::Fd fd = net::connect_tcp("127.0.0.1", server->port(), net::Deadline::after(1s));
    const std::vector<std::uint8_t> junk{1, 99, 0xFF};
    ASSERT_EQ(net::write_frame(fd, junk, net::Deadline::after(1s)), net::IoStatus::kOk);
    const auto f = net::read_frame(fd, net::Deadline::after(1s));
    ASSERT_EQ(f.status, net::IoStatus::kOk);
    EXPECT_TRUE(std::holds_alternative<net::ErrorMessage>(net::decode(f.payload)));
  }
  // A client announcing a 4 GiB frame is dropped too.
  {
    const net::Fd fd = net::connect_tcp("127.0.0.1", server->port(), net::Deadline::after(1s));
    const std::vector<std::uint8_t> huge{0xFF, 0xFF, 0xFF, 0xFF};
    ASSERT_EQ(net::write_all(fd, huge, net::Deadline::after(1s)), net::IoStatus::kOk);
    EXPECT_EQ(net::read_frame(fd, net::Deadline::after(2s)).status, net::IoStatus::kClosed);
  }
  const auto ok =
      net::call("127.0.0.1", server->port(), net::ShardSearchRequest{"salah", 1, 0, 0, {}},
                net::Deadline::after(2s));
  EXPECT_TRUE(ok.has_value());
}

TEST(ShardServerTest, UnsupportedRequestGetsError) {
  const auto server = start_server();
  EXPECT_TRUE(std::holds_alternative<net::ErrorMessage>(server->handle(net::SearchRequest{})));
}

TEST(ShardServerTest, StopIsPromptEvenWithIdleConnections) {
  auto server = start_server();
  const net::Fd idle = net::connect_tcp("127.0.0.1", server->port(), net::Deadline::after(1s));
  std::this_thread::sleep_for(50ms);  // let a worker pick it up and sit waiting
  const auto t0 = std::chrono::steady_clock::now();
  server->stop();
  EXPECT_LT(std::chrono::steady_clock::now() - t0, 1s);
  server.reset();  // destructor after stop: idempotent
}

TEST(ShardServerTest, RejectsInconsistentConfiguration) {
  auto corpus = corpus::load_tsv_file("tests/fixtures/mini_corpus.tsv");
  auto index = index::build_index(corpus, text::Analyzer{});
  EXPECT_THROW(
      ShardServer(std::move(corpus), std::move(index), {}, {.shard_id = 3, .num_shards = 3}),
      Error);
}

}  // namespace
}  // namespace gaffer::server
