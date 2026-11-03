#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "net/messages.hpp"
#include "net/socket.hpp"

namespace gaffer::server {

struct TcpServerOptions {
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;  // 0 = pick a free port (see TcpServer::port)
  std::size_t worker_threads = 4;
  std::size_t max_queued_connections = 64;  // ready-but-unserved connections; beyond: refuse
  std::size_t max_connections = 1024;       // open connections in total; beyond: refuse
  std::chrono::milliseconds io_timeout{5000};
};

// Request/response server over the framed protocol, shared by the shard
// server and the broker.
//
// Threading model: a reactor with a worker pool.
//
//   poller thread: poll() over [wake-up pipe, listener, every idle connection]
//     - listener readable    -> accept new connections into the idle set
//     - connection readable  -> move it to the work queue (a request is arriving,
//                               or the peer hung up)
//   N worker threads: take a connection, serve exactly ONE request on it, then
//     hand it back to the poller (via the returned list + a byte on the
//     wake-up pipe) and pick up the next ready connection.
//
// Why not one thread per connection: clients keep connections open between
// requests (the broker pools them). With thread-per-connection, N idle pooled
// connections pin all N workers and new connections starve until they time
// out. A broker test with 8 concurrent clients hit exactly that. Here idle
// connections cost a pollfd, not a thread.
//
// Back-pressure: if the work queue is full, a ready connection is closed
// (callers see a failed node, not unbounded queueing latency).
// Shutdown: std::jthread + stop_token everywhere; poll() and the workers'
// condition-variable waits wake within 100 ms or immediately on stop().
//
// The handler is called concurrently from several workers and must be thread-safe.
class TcpServer {
 public:
  using Handler = std::function<net::Message(const net::Message&)>;

  TcpServer(Handler handler, TcpServerOptions options);
  ~TcpServer();
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;
  TcpServer(TcpServer&&) = delete;
  TcpServer& operator=(TcpServer&&) = delete;

  // Binds and starts the threads. Throws gaffer::IoError if it can't bind.
  void start();
  // Stops accepting, finishes in-flight requests, joins all threads. Idempotent.
  void stop();

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  [[nodiscard]] std::uint64_t requests_served() const noexcept { return served_.load(); }

 private:
  void poll_loop(const std::stop_token& stop);
  void worker_loop(const std::stop_token& stop);
  // Serves one request; returns false if the connection should be closed.
  bool serve_one(const net::Fd& conn);
  void wake_poller() const noexcept;

  Handler handler_;
  TcpServerOptions options_;
  net::Fd listener_;
  net::Fd wake_read_;  // self-pipe: workers write a byte to interrupt poll()
  net::Fd wake_write_;
  std::uint16_t port_ = 0;
  std::mutex mu_;
  std::condition_variable_any cv_;
  std::deque<net::Fd> ready_;         // guarded by mu_: readable connections for workers
  std::vector<net::Fd> returned_;     // guarded by mu_: served connections back to the poller
  std::atomic<std::size_t> open_{0};  // connections currently open (idle + ready + in service)
  std::atomic<std::uint64_t> served_{0};
  std::vector<std::jthread> threads_;
};

}  // namespace gaffer::server
