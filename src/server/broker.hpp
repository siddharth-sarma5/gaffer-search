#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "net/messages.hpp"
#include "net/socket.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "server/tcp_server.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {

struct ShardAddress {
  std::string host;
  std::uint16_t port = 0;
};

// Parses "host:port". Throws gaffer::Error if malformed.
[[nodiscard]] ShardAddress parse_address(const std::string& text);

struct BrokerOptions {
  std::vector<ShardAddress> shards;
  // Budget for one fan-out: every shard call in a query shares this deadline.
  std::chrono::milliseconds shard_deadline{200};
  // How long start() keeps retrying shards that aren't up yet.
  std::chrono::milliseconds startup_timeout{30000};
  TcpServerOptions tcp;
};

// The query front-end of the sharded deployment.
//
// start():  asks every shard for its statistics (retrying while shards boot),
//           merges them into global statistics (merge_stats), then listens.
// search(): plans the query (no index needed), attaches the global df of its
//           terms, sends it to all shards in parallel, and merges their top-k
//           lists into the global top-k.
//
// Fan-out: one std::jthread per shard per query. Each writes only its own
// result slot, so there's no shared mutable state to lock, and all threads
// share one Deadline, so the whole fan-out finishes within shard_deadline
// (plus scheduling noise). Joined before merging; nothing outlives the request.
//
// Failure: a shard that times out, refuses the connection, hangs up or replies
// with an error is listed in SearchResponse::missing_shards, and the client gets
// the merged results of the shards that did answer. The query still returns
// within shard_deadline: a slow shard costs completeness, not latency
// ("partial results beat late results"). Index order in missing_shards is the
// --shard order the broker was given.
//
// Connections: each shard has a small pool of open connections. A call takes
// an idle one or opens a new one, and returns it to the pool on success. A
// pooled connection can be stale (e.g. the shard restarted); if it fails
// before the deadline, the call retries once on a fresh connection.
class Broker {
 public:
  Broker(query::NicknameExpander expander, BrokerOptions options);
  ~Broker();
  Broker(const Broker&) = delete;
  Broker& operator=(const Broker&) = delete;
  Broker(Broker&&) = delete;
  Broker& operator=(Broker&&) = delete;

  // Gathers global statistics, then starts serving. Throws gaffer::Error if a
  // shard is still unreachable after startup_timeout or the cluster is inconsistent.
  void start();
  void stop();
  [[nodiscard]] std::uint16_t port() const noexcept { return tcp_.port(); }

  // Thread-safe; public so tests and benchmarks can call it without a socket.
  [[nodiscard]] net::Message handle(const net::Message& request);
  [[nodiscard]] net::SearchResponse search(const net::SearchRequest& request);

  [[nodiscard]] const query::GlobalStats& global_stats() const noexcept { return global_; }

 private:
  struct ShardChannel {
    ShardAddress address;
    std::mutex mu;
    std::vector<net::Fd> idle;  // guarded by mu
  };

  // One request to shard i on a pooled connection. nullopt on failure.
  std::optional<net::Message> call_shard(std::size_t i, const net::Message& request,
                                         net::Deadline deadline);
  void gather_stats();

  query::NicknameExpander expander_;
  BrokerOptions options_;
  std::vector<std::unique_ptr<ShardChannel>> channels_;
  query::GlobalStats global_;
  text::Analyzer analyzer_;  // set from the shards' analyzer flags in gather_stats
  TcpServer tcp_;            // last: its threads call handle()
};

// The global top-k from per-shard lists (each best-first): a k-way merge with
// a heap over the list heads, O(k log S) for S shards. Order is ranks_before:
// score descending, then global DocId ascending, exactly as on a single node.
[[nodiscard]] std::vector<net::Hit> merge_top_k(const std::vector<std::vector<net::Hit>>& lists,
                                                std::size_t k);

}  // namespace gaffer::server
