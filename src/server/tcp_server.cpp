#include "server/tcp_server.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "common/error.hpp"
#include "net/framing.hpp"
#include "net/messages.hpp"
#include "net/socket.hpp"

namespace gaffer::server {

namespace {

constexpr int kPollSliceMs = 100;  // how often the poller re-checks its stop_token

}  // namespace

TcpServer::TcpServer(Handler handler, TcpServerOptions options)
    : handler_(std::move(handler)), options_(std::move(options)) {}

TcpServer::~TcpServer() { stop(); }

void TcpServer::start() {
  listener_ = net::listen_tcp(options_.host, options_.port);
  port_ = net::local_port(listener_);
  std::array<int, 2> fds{-1, -1};
  if (pipe2(fds.data(), O_NONBLOCK | O_CLOEXEC) != 0) {
    throw IoError("pipe2() failed");
  }
  wake_read_ = net::Fd(fds[0]);
  wake_write_ = net::Fd(fds[1]);
  threads_.emplace_back([this](const std::stop_token& st) { poll_loop(st); });
  for (std::size_t i = 0; i < options_.worker_threads; ++i) {
    threads_.emplace_back([this](const std::stop_token& st) { worker_loop(st); });
  }
}

void TcpServer::stop() {
  for (auto& t : threads_) {
    t.request_stop();
  }
  cv_.notify_all();
  wake_poller();
  threads_.clear();  // jthread destructors join
  const std::scoped_lock lock(mu_);
  ready_.clear();
  returned_.clear();
  listener_.reset();
  wake_read_.reset();
  wake_write_.reset();
}

void TcpServer::wake_poller() const noexcept {
  if (wake_write_.valid()) {
    const char b = 1;
    // Ignoring failure is correct: a full pipe already means "wake up".
    [[maybe_unused]] const ssize_t n = ::write(wake_write_.get(), &b, 1);
  }
}

void TcpServer::poll_loop(const std::stop_token& stop) {
  std::vector<net::Fd> idle;  // owned by this thread only
  std::vector<pollfd> fds;
  while (!stop.stop_requested()) {
    fds.clear();
    fds.push_back({wake_read_.get(), POLLIN, 0});
    fds.push_back({listener_.get(), POLLIN, 0});
    for (const auto& c : idle) {
      fds.push_back({c.get(), POLLIN, 0});
    }
    if (::poll(fds.data(), fds.size(), kPollSliceMs) < 0) {
      continue;  // EINTR: re-check stop and rebuild
    }

    if ((fds[0].revents & POLLIN) != 0) {
      std::array<char, 64> buf{};  // drain the wake-up bytes
      while (::read(wake_read_.get(), buf.data(), buf.size()) > 0) {
      }
    }
    // Readable idle connections go to the workers. Iterate backwards so
    // erasing doesn't shift entries we haven't visited (fds[i + 2] <-> idle[i]).
    for (std::size_t i = idle.size(); i-- > 0;) {
      if (fds[i + 2].revents == 0) {
        continue;
      }
      net::Fd conn = std::move(idle[i]);
      idle.erase(idle.begin() + static_cast<std::ptrdiff_t>(i));
      {
        const std::scoped_lock lock(mu_);
        if (ready_.size() < options_.max_queued_connections) {
          ready_.push_back(std::move(conn));
          cv_.notify_one();
          continue;
        }
      }
      open_.fetch_sub(1);  // back-pressure: conn closes as it goes out of scope
    }
    // Connections the workers finished with come back to the idle set.
    {
      const std::scoped_lock lock(mu_);
      for (auto& c : returned_) {
        idle.push_back(std::move(c));
      }
      returned_.clear();
    }
    // New connections.
    if ((fds[1].revents & POLLIN) != 0) {
      while (true) {
        net::Fd conn = net::accept_until(listener_, net::Deadline{net::Clock::now()});
        if (!conn.valid()) {
          break;
        }
        if (open_.load() >= options_.max_connections) {
          continue;  // over the limit: closed at end of scope
        }
        open_.fetch_add(1);
        idle.push_back(std::move(conn));
      }
    }
  }
}

void TcpServer::worker_loop(const std::stop_token& stop) {
  while (!stop.stop_requested()) {
    net::Fd conn;
    {
      std::unique_lock lock(mu_);
      // condition_variable_any + stop_token: the wait also wakes on stop.
      if (!cv_.wait(lock, stop, [this] { return !ready_.empty(); })) {
        return;
      }
      conn = std::move(ready_.front());
      ready_.pop_front();
    }
    if (serve_one(conn)) {
      {
        const std::scoped_lock lock(mu_);
        returned_.push_back(std::move(conn));
      }
      wake_poller();
    } else {
      open_.fetch_sub(1);  // conn closes as it goes out of scope
    }
  }
}

bool TcpServer::serve_one(const net::Fd& conn) {
  const auto deadline = net::Deadline::after(options_.io_timeout);
  net::FrameResult frame = net::read_frame(conn, deadline);
  if (frame.status != net::IoStatus::kOk) {
    return false;  // peer hung up, timed out mid-frame, or oversized frame
  }
  net::Message reply;
  try {
    reply = handler_(net::decode(frame.payload));
  } catch (const FormatError& e) {
    // Garbage in: answer with an error, then hang up on this peer.
    (void)net::write_frame(conn, net::encode(net::ErrorMessage{e.what()}), deadline);
    return false;
  } catch (const Error& e) {
    reply = net::ErrorMessage{e.what()};  // a handler failure is reported, not fatal
  }
  if (net::write_frame(conn, net::encode(reply), deadline) != net::IoStatus::kOk) {
    return false;
  }
  served_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

}  // namespace gaffer::server
