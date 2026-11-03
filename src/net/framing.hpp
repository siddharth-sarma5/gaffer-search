#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "net/socket.hpp"

namespace gaffer::net {

// Length-prefixed framing over a TCP byte stream:
//
//   [u32 little-endian payload length][payload bytes]
//
// TCP delivers a stream of bytes, not messages: one send can arrive in pieces
// and two sends can arrive glued together. The length prefix restores message
// boundaries: read 4 bytes, then read exactly that many.
//
// The length is checked against a maximum *before* allocating. Otherwise a
// peer sending 0xFFFFFFFF could make us allocate 4 GiB. An oversized length
// can't be skipped safely (we'd have to read gigabytes to resync), so the
// connection must be closed.
inline constexpr std::uint32_t kMaxFrameBytes = 4U << 20U;  // 4 MiB

[[nodiscard]] IoStatus write_frame(const Fd& fd, std::span<const std::uint8_t> payload,
                                   Deadline deadline);

struct FrameResult {
  IoStatus status = IoStatus::kOk;
  bool too_large = false;  // status is kError and the announced length exceeded the cap
  std::vector<std::uint8_t> payload;
};

[[nodiscard]] FrameResult read_frame(const Fd& fd, Deadline deadline,
                                     std::uint32_t max_bytes = kMaxFrameBytes);

}  // namespace gaffer::net
