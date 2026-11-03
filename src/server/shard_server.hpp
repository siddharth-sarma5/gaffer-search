#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "net/messages.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "server/tcp_server.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {

struct ShardServerOptions {
  std::uint32_t shard_id = 0;
  std::uint32_t num_shards = 1;
  TcpServerOptions tcp;
  // Fault injection for tests and benchmarks: wait this long before
  // answering a search, simulating a slow (GC-pausing, overloaded) shard.
  std::chrono::milliseconds search_delay{0};
};

// Serves one shard of the index over TCP (threading: see TcpServer). Answers
// StatsRequest (for the broker's global statistics) and ShardSearchRequest.
class ShardServer {
 public:
  ShardServer(corpus::Corpus corpus, index::InvertedIndex index, query::NicknameExpander expander,
              ShardServerOptions options);
  ~ShardServer();
  ShardServer(const ShardServer&) = delete;
  ShardServer& operator=(const ShardServer&) = delete;
  ShardServer(ShardServer&&) = delete;
  ShardServer& operator=(ShardServer&&) = delete;

  // Binds and starts the threads. Throws gaffer::IoError if it can't bind.
  void start();
  // Stops accepting, finishes in-flight requests, joins all threads. Idempotent.
  void stop();

  [[nodiscard]] std::uint16_t port() const noexcept { return tcp_.port(); }
  [[nodiscard]] std::uint64_t requests_served() const noexcept { return tcp_.requests_served(); }

  // Handles one decoded request. Public so tests can exercise it without sockets.
  [[nodiscard]] net::Message handle(const net::Message& request) const;

 private:
  [[nodiscard]] net::Message search(const net::ShardSearchRequest& req) const;
  [[nodiscard]] net::Message stats() const;

  corpus::Corpus corpus_;
  index::InvertedIndex index_;
  query::NicknameExpander expander_;
  ShardServerOptions options_;
  query::Searcher searcher_;
  text::Analyzer analyzer_;
  TcpServer tcp_;  // last: its threads call handle(), so it must stop before the rest is destroyed
};

}  // namespace gaffer::server
