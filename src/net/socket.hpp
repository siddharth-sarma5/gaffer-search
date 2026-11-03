#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace gaffer::net {

using Clock = std::chrono::steady_clock;

// An absolute point in time by which an operation must finish.
//
// Why a deadline and not a timeout: a request does several reads and writes.
// With a per-call timeout of 100 ms, five slow calls take 500 ms. With one
// deadline passed down to every call, the whole request finishes within its
// budget. That's what lets the broker promise "partial results within X ms".
struct Deadline {
  Clock::time_point at;

  [[nodiscard]] static Deadline after(std::chrono::milliseconds d) { return {Clock::now() + d}; }
  [[nodiscard]] static Deadline never() { return {Clock::time_point::max()}; }
  [[nodiscard]] bool expired() const { return Clock::now() >= at; }
  // Milliseconds left, clamped to [0, INT_MAX] for poll(); -1 means "wait forever".
  [[nodiscard]] int poll_timeout_ms() const;
};

// Outcome of an I/O operation. Network failures are expected events, not
// exceptional ones, so they're values (D-009), never exceptions.
enum class IoStatus : std::uint8_t {
  kOk,
  kTimeout,  // the deadline passed
  kClosed,   // the peer closed the connection (EOF mid-read, or EPIPE/ECONNRESET)
  kError,    // any other socket error
};

[[nodiscard]] const char* to_string(IoStatus s) noexcept;

// Owning file descriptor (RAII): closed exactly once, in the destructor.
// Move-only, like std::unique_ptr, because two owners would double-close.
class Fd {
 public:
  Fd() = default;
  explicit Fd(int fd) noexcept : fd_(fd) {}
  ~Fd();
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  Fd& operator=(Fd&& other) noexcept;

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
  void reset() noexcept;

 private:
  int fd_ = -1;
};

// Listening TCP socket on host:port (port 0 = let the OS pick; see local_port).
// SO_REUSEADDR is set so a restarted server can rebind immediately.
// Throws gaffer::IoError on failure.
[[nodiscard]] Fd listen_tcp(const std::string& host, std::uint16_t port, int backlog = 128);

// The port a socket is bound to (useful after listening on port 0).
[[nodiscard]] std::uint16_t local_port(const Fd& fd);

// Connects to host:port before `deadline`. Non-blocking connect + poll, so a
// black-holed host can't stall the caller past its deadline. Returns an
// invalid Fd on failure (refused, unreachable, timed out); never throws.
[[nodiscard]] Fd connect_tcp(const std::string& host, std::uint16_t port, Deadline deadline,
                             IoStatus* why = nullptr);

// Waits for a connection until `deadline`. Returns an invalid Fd on timeout.
[[nodiscard]] Fd accept_until(const Fd& listener, Deadline deadline);

// Waits until fd has data to read (or EOF) without consuming anything.
// Lets an idle server poll in short slices for shutdown while never
// cutting a frame in half: once data is ready, the frame is read under the
// full request deadline.
[[nodiscard]] IoStatus wait_readable(const Fd& fd, Deadline deadline);

// Reads exactly buf.size() bytes, looping over partial reads (TCP is a byte
// stream: one write on the sender can arrive as many reads here, and vice
// versa). Retries on EINTR; waits with poll() between attempts.
[[nodiscard]] IoStatus read_exact(const Fd& fd, std::span<std::uint8_t> buf, Deadline deadline);

// Writes all of buf, looping over partial writes. Uses MSG_NOSIGNAL so writing
// to a closed peer returns kClosed instead of killing the process with SIGPIPE.
[[nodiscard]] IoStatus write_all(const Fd& fd, std::span<const std::uint8_t> buf,
                                 Deadline deadline);

}  // namespace gaffer::net
