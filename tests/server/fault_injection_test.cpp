// Fault injection against a real in-process cluster: restarts, garbage,
// black holes, late starters, and shards dying under concurrent load.
// Run under TSan in CI.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "cluster.hpp"
#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "index/partition.hpp"
#include "net/framing.hpp"
#include "net/messages.hpp"
#include "net/socket.hpp"
#include "query/nicknames.hpp"
#include "server/broker.hpp"
#include "server/shard_server.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {
namespace {

using namespace std::chrono_literals;

class FaultInjectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    corpus_ = corpus::load_tsv_file("data/corpus/pl-2025-26.tsv");
    expander_ = query::NicknameExpander::load_tsv_file("data/nicknames.tsv");
  }

  std::unique_ptr<ShardServer> make_shard(std::uint32_t s, std::uint32_t n, std::uint16_t port) {
    auto part = index::partition(corpus_, n)[s];
    auto idx = index::build_index(part, text::Analyzer{});
    ShardServerOptions o{.shard_id = s, .num_shards = n, .tcp = {}};
    o.tcp.port = port;
    return std::make_unique<ShardServer>(std::move(part), std::move(idx), expander_, o);
  }

  corpus::Corpus corpus_;
  query::NicknameExpander expander_;
};

// A minimal fake peer: accepts connections and either answers every frame
// with garbage, or (black hole) reads and never answers.
class FakeShard {
 public:
  enum class Mode : std::uint8_t { kGarbage, kBlackHole };
  explicit FakeShard(Mode mode, std::uint16_t port = 0)
      : listener_(net::listen_tcp("127.0.0.1", port)) {
    thread_ = std::jthread([this, mode](const std::stop_token& stop) {
      std::vector<net::Fd> held;
      while (!stop.stop_requested()) {
        net::Fd c = net::accept_until(listener_, net::Deadline::after(50ms));
        if (!c.valid()) {
          continue;
        }
        const auto f = net::read_frame(c, net::Deadline::after(1s));
        if (mode == Mode::kGarbage && f.status == net::IoStatus::kOk) {
          const std::vector<std::uint8_t> junk{1, 200, 7, 7, 7};
          (void)net::write_frame(c, junk, net::Deadline::after(1s));
        }
        held.push_back(std::move(c));  // keep it open (black hole: never answer)
      }
    });
  }
  [[nodiscard]] std::uint16_t port() const { return net::local_port(listener_); }

 private:
  net::Fd listener_;
  std::jthread thread_;  // declared last: stops and joins before listener_ closes
};

TEST_F(FaultInjectionTest, ShardRestartOnSamePortRecoversThroughStalePoolRetry) {
  testing::Cluster cluster(corpus_, 3, expander_);
  ASSERT_FALSE(cluster.broker->search({"salah", 10}).partial());  // pools now hold connections

  const std::uint16_t port = cluster.shards[1]->port();
  cluster.shards[1]->stop();
  cluster.shards[1] = make_shard(1, 3, port);  // same id, same port: a restart
  cluster.shards[1]->start();

  // The broker's pooled connection to shard 1 is dead; call_shard must notice
  // and retry on a fresh connection within the same request.
  const auto r = cluster.broker->search({"salah", 10});
  EXPECT_FALSE(r.partial()) << "missing " << r.missing_shards.size();
}

TEST_F(FaultInjectionTest, GarbageAtStartupFailsStartup) {
  const auto real = make_shard(0, 2, 0);
  real->start();
  const FakeShard fake(FakeShard::Mode::kGarbage);
  BrokerOptions bo;
  bo.shards = {{"127.0.0.1", real->port()}, {"127.0.0.1", fake.port()}};
  bo.startup_timeout = 300ms;
  Broker broker(expander_, bo);
  // Startup needs stats from every shard; garbage isn't stats.
  EXPECT_THROW(broker.start(), Error);
}

// After startup, shard 1 is replaced (same port) by a peer that accepts and
// reads but never answers. The broker's stale pooled connection fails, the
// retry connects to the black hole, and the shared deadline cuts it off.
TEST_F(FaultInjectionTest, BlackHoledShardTripsTheDeadline) {
  testing::Cluster cluster(corpus_, 2, expander_, {.shard_deadline = 120ms, .delays = {}});
  ASSERT_FALSE(cluster.broker->search({"arsenal", 5}).partial());
  const std::uint16_t port = cluster.shards[1]->port();
  cluster.shards[1]->stop();
  const FakeShard hole(FakeShard::Mode::kBlackHole, port);

  const auto t0 = std::chrono::steady_clock::now();
  const auto r = cluster.broker->search({"arsenal", 5});
  const auto took = std::chrono::steady_clock::now() - t0;
  EXPECT_EQ(r.missing_shards, (std::vector<std::uint32_t>{1}));
  EXPECT_GE(took, 110ms);
  EXPECT_LT(took, 600ms);
  EXPECT_FALSE(r.hits.empty());  // shard 0 still answered
}

// Same swap, but the impostor answers with an undecodable frame.
TEST_F(FaultInjectionTest, GarbageSpeakingShardIsMarkedMissing) {
  testing::Cluster cluster(corpus_, 2, expander_);
  ASSERT_FALSE(cluster.broker->search({"arsenal", 5}).partial());
  const std::uint16_t port = cluster.shards[1]->port();
  cluster.shards[1]->stop();
  const FakeShard liar(FakeShard::Mode::kGarbage, port);
  const auto r = cluster.broker->search({"arsenal", 5});
  EXPECT_EQ(r.missing_shards, (std::vector<std::uint32_t>{1}));
}

TEST_F(FaultInjectionTest, BrokerWaitsForAShardThatBootsLate) {
  const auto s0 = make_shard(0, 2, 0);
  s0->start();
  // Reserve a port for shard 1, then start it 400 ms after the broker.
  std::uint16_t port1 = 0;
  {
    const net::Fd probe = net::listen_tcp("127.0.0.1", 0);
    port1 = net::local_port(probe);
  }
  BrokerOptions bo;
  bo.shards = {{"127.0.0.1", s0->port()}, {"127.0.0.1", port1}};
  bo.startup_timeout = 5000ms;
  Broker broker(expander_, bo);
  std::unique_ptr<ShardServer> s1;
  std::jthread late([&] {
    std::this_thread::sleep_for(400ms);
    s1 = make_shard(1, 2, port1);
    s1->start();
  });
  broker.start();  // blocks, retrying, until shard 1 answers
  late.join();
  EXPECT_EQ(broker.global_stats().doc_count, corpus_.size());
  EXPECT_FALSE(broker.search({"salah", 5}).partial());
  broker.stop();
  s1->stop();
}

// Under TSan: concurrent clients while a shard is stopped mid-flight. Every
// response must be either complete or correctly flagged; no crash, no race.
TEST_F(FaultInjectionTest, ConcurrentQueriesWhileAShardDies) {
  testing::Cluster cluster(corpus_, 3, expander_, {.shard_deadline = 500ms, .delays = {}});
  std::atomic<int> complete{0};
  std::atomic<int> partial{0};
  std::atomic<int> bad{0};
  {
    std::vector<std::jthread> clients;
    for (int c = 0; c < 6; ++c) {
      clients.emplace_back([&] {
        for (int i = 0; i < 30; ++i) {
          const auto r = cluster.broker->search({"late winner", 10});
          if (!r.partial()) {
            ++complete;
          } else if (r.missing_shards == std::vector<std::uint32_t>{2}) {
            ++partial;
          } else {
            ++bad;
          }
        }
      });
    }
    std::this_thread::sleep_for(30ms);
    cluster.shards[2]->stop();
  }  // clients join
  EXPECT_EQ(bad.load(), 0);
  EXPECT_EQ(complete.load() + partial.load(), 180);
  EXPECT_GT(partial.load(), 0);
}

}  // namespace
}  // namespace gaffer::server
