#include "net/framing.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "net/socket.hpp"

namespace gaffer::net {

IoStatus write_frame(const Fd& fd, std::span<const std::uint8_t> payload, Deadline deadline) {
  if (payload.size() > kMaxFrameBytes) {
    return IoStatus::kError;  // we never send what a peer would have to reject
  }
  // Header and payload in one buffer -> one send() in the common case (with
  // Nagle disabled, two sends could leave as two packets).
  std::vector<std::uint8_t> buf;
  buf.reserve(4 + payload.size());
  const auto n = static_cast<std::uint32_t>(payload.size());
  for (unsigned shift = 0; shift < 32; shift += 8) {
    buf.push_back(static_cast<std::uint8_t>(n >> shift));
  }
  buf.insert(buf.end(), payload.begin(), payload.end());
  return write_all(fd, buf, deadline);
}

FrameResult read_frame(const Fd& fd, Deadline deadline, std::uint32_t max_bytes) {
  FrameResult r;
  std::array<std::uint8_t, 4> header{};
  r.status = read_exact(fd, header, deadline);
  if (r.status != IoStatus::kOk) {
    return r;
  }
  std::uint32_t n = 0;
  for (unsigned i = 0; i < 4; ++i) {
    n |= static_cast<std::uint32_t>(header[i]) << (8U * i);
  }
  if (n > max_bytes) {
    r.status = IoStatus::kError;
    r.too_large = true;
    return r;
  }
  r.payload.resize(n);
  r.status = read_exact(fd, r.payload, deadline);
  return r;
}

}  // namespace gaffer::net
