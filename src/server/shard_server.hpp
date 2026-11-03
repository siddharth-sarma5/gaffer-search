#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "net/messages.hpp"
#include "net/socket.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {

struct ShardServerOptions {
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;  // 0 = pick a free port (see ShardServer::port)
  std::uint32_t shard_id = 0;
  std::uint32_t num_shards = 1;
  std::size_t worker_threads = 4;
  std::size_t max_queued_connections = 64;  // back-pressure: beyond this, refuse
  std::chrono::milliseconds io_timeout{5000};
  // Fault injection for tests and benchmarks: wait this long before
  // answering a search, simulating a slow (GC-pausing, overloaded) shard.
  std::chrono::milliseconds search_delay{0};
};

// Serves one shard of the index over TCP.
//
// Threading model:
//   accept thread  --(bounded queue of connections)-->  N worker threads
// Each worker owns one connection at a time and serves requests on it until
// the client closes it (connections are long-lived: the broker keeps a pool).
// If the queue is full, new connections are closed at once (back-pressure:
// the broker sees a failed shard and returns partial results rather than
// piling up latency).
//
// Shutdown: every thread is a std::jthread whose stop_token is checked
// between blocking calls. Blocking waits are bounded (100 ms slices), so
// stop() returns promptly even with idle connections open.
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

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  [[nodiscard]] std::uint64_t requests_served() const noexcept { return served_.load(); }

  // Handles one decoded request. Public so tests can exercise it without sockets.
  [[nodiscard]] net::Message handle(const net::Message& request) const;

 private:
  void accept_loop(const std::stop_token& stop);
  void worker_loop(const std::stop_token& stop);
  void serve(const net::Fd& conn, const std::stop_token& stop);
  [[nodiscard]] net::Message search(const net::ShardSearchRequest& req) const;

  corpus::Corpus corpus_;
  index::InvertedIndex index_;
  query::NicknameExpander expander_;
  ShardServerOptions options_;
  query::Searcher searcher_;
  text::Analyzer analyzer_;

  net::Fd listener_;
  std::uint16_t port_ = 0;
  std::mutex mu_;
  std::condition_variable_any cv_;
  std::deque<net::Fd> queue_;  // guarded by mu_
  std::atomic<std::uint64_t> served_{0};
  std::vector<std::jthread> threads_;
};

}  // namespace gaffer::server
