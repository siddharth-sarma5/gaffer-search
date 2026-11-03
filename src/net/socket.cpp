#include "net/socket.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/error.hpp"

namespace gaffer::net {

namespace {

// The BSD socket API takes a generic sockaddr* for every address family, so a
// cast from sockaddr_in is unavoidable. It's confined to these two helpers.
sockaddr* as_sockaddr(sockaddr_in& a) noexcept {
  return reinterpret_cast<sockaddr*>(&a);  // NOLINT(*-reinterpret-cast)
}
const sockaddr* as_sockaddr(const sockaddr_in& a) noexcept {
  return reinterpret_cast<const sockaddr*>(&a);  // NOLINT(*-reinterpret-cast)
}

void set_nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);  // NOLINT(cppcoreguidelines-pro-type-vararg)
  if (flags < 0 ||
      fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
    throw IoError("fcntl(O_NONBLOCK) failed");
  }
}

// Requests are small and latency-sensitive: disable Nagle's algorithm so a
// small frame isn't held back waiting for more data to coalesce.
void set_nodelay(int fd) {
  const int one = 1;
  (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

// Waits until fd is ready for `events` or the deadline passes.
IoStatus wait_for(int fd, short events, Deadline deadline) {
  while (true) {
    pollfd p{fd, events, 0};
    const int r = poll(&p, 1, deadline.poll_timeout_ms());
    if (r > 0) {
      return IoStatus::kOk;  // ready (or error/hangup, which the next syscall reports)
    }
    if (r == 0) {
      return IoStatus::kTimeout;
    }
    if (errno != EINTR) {
      return IoStatus::kError;
    }
    if (deadline.expired()) {
      return IoStatus::kTimeout;
    }
  }
}

sockaddr_in resolve_ipv4(const std::string& host, std::uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (host.empty() || host == "0.0.0.0") {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    return addr;
  }
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) {
    return addr;
  }
  // A hostname (e.g. "shard0" inside docker-compose): resolve it. getaddrinfo
  // blocks, but it's only used when (re)connecting, not per request.
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
    throw IoError("cannot resolve host '" + host + "'");
  }
  std::copy_n(reinterpret_cast<const sockaddr_in*>(res->ai_addr),  // NOLINT(*-reinterpret-cast)
              1, &addr);
  freeaddrinfo(res);
  addr.sin_port = htons(port);
  return addr;
}

}  // namespace

int Deadline::poll_timeout_ms() const {
  if (at == Clock::time_point::max()) {
    return -1;
  }
  const auto left = std::chrono::ceil<std::chrono::milliseconds>(at - Clock::now()).count();
  return static_cast<int>(std::clamp<long long>(left, 0, INT_MAX));
}

const char* to_string(IoStatus s) noexcept {
  switch (s) {
    case IoStatus::kOk:
      return "ok";
    case IoStatus::kTimeout:
      return "timeout";
    case IoStatus::kClosed:
      return "closed";
    case IoStatus::kError:
      return "error";
  }
  return "unknown";
}

Fd::~Fd() { reset(); }

Fd& Fd::operator=(Fd&& other) noexcept {
  if (this != &other) {
    reset();
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

void Fd::reset() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);  // not retried on EINTR: on Linux the fd is released regardless
    fd_ = -1;
  }
}

Fd listen_tcp(const std::string& host, std::uint16_t port, int backlog) {
  Fd fd(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid()) {
    throw IoError("socket() failed");
  }
  const int one = 1;
  (void)setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  const sockaddr_in addr = resolve_ipv4(host, port);
  if (::bind(fd.get(), as_sockaddr(addr), sizeof(addr)) != 0) {
    throw IoError("cannot bind " + host + ":" + std::to_string(port));
  }
  if (::listen(fd.get(), backlog) != 0) {
    throw IoError("listen() failed");
  }
  set_nonblocking(fd.get());
  return fd;
}

std::uint16_t local_port(const Fd& fd) {
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (getsockname(fd.get(), as_sockaddr(addr), &len) != 0) {
    throw IoError("getsockname() failed");
  }
  return ntohs(addr.sin_port);
}

Fd connect_tcp(const std::string& host, std::uint16_t port, Deadline deadline, IoStatus* why) {
  auto fail = [&](IoStatus s) {
    if (why != nullptr) {
      *why = s;
    }
    return Fd{};
  };
  sockaddr_in addr{};
  try {
    addr = resolve_ipv4(host, port);
  } catch (const IoError&) {
    return fail(IoStatus::kError);
  }
  Fd fd(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
  if (!fd.valid()) {
    return fail(IoStatus::kError);
  }
  if (::connect(fd.get(), as_sockaddr(addr), sizeof(addr)) != 0) {
    if (errno != EINPROGRESS) {
      return fail(errno == ECONNREFUSED ? IoStatus::kClosed : IoStatus::kError);
    }
    // Non-blocking connect in progress: wait for writability, then read the
    // real outcome from SO_ERROR.
    const IoStatus w = wait_for(fd.get(), POLLOUT, deadline);
    if (w != IoStatus::kOk) {
      return fail(w);
    }
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
      return fail(err == ECONNREFUSED ? IoStatus::kClosed : IoStatus::kError);
    }
  }
  set_nodelay(fd.get());
  if (why != nullptr) {
    *why = IoStatus::kOk;
  }
  return fd;
}

Fd accept_until(const Fd& listener, Deadline deadline) {
  while (true) {
    Fd conn(::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK));
    if (conn.valid()) {
      set_nodelay(conn.get());
      return conn;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ECONNABORTED) {
      return Fd{};
    }
    if (wait_for(listener.get(), POLLIN, deadline) != IoStatus::kOk) {
      return Fd{};
    }
  }
}

IoStatus read_exact(const Fd& fd, std::span<std::uint8_t> buf, Deadline deadline) {
  std::size_t done = 0;
  while (done < buf.size()) {
    const ssize_t n = ::recv(fd.get(), buf.data() + done, buf.size() - done, 0);
    if (n > 0) {
      done += static_cast<std::size_t>(n);
      continue;
    }
    if (n == 0) {
      return IoStatus::kClosed;  // EOF before we had everything
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == ECONNRESET) {
      return IoStatus::kClosed;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      return IoStatus::kError;
    }
    if (const IoStatus w = wait_for(fd.get(), POLLIN, deadline); w != IoStatus::kOk) {
      return w;
    }
  }
  return IoStatus::kOk;
}

IoStatus write_all(const Fd& fd, std::span<const std::uint8_t> buf, Deadline deadline) {
  std::size_t done = 0;
  while (done < buf.size()) {
    const ssize_t n = ::send(fd.get(), buf.data() + done, buf.size() - done, MSG_NOSIGNAL);
    if (n > 0) {
      done += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EPIPE || errno == ECONNRESET)) {
      return IoStatus::kClosed;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      return IoStatus::kError;
    }
    if (const IoStatus w = wait_for(fd.get(), POLLOUT, deadline); w != IoStatus::kOk) {
      return w;
    }
  }
  return IoStatus::kOk;
}

}  // namespace gaffer::net
