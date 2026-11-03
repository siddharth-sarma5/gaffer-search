#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "net/messages.hpp"
#include "net/socket.hpp"

namespace gaffer::net {

// One request/response exchange on an open connection: write a frame, read a
// frame, decode it. Returns nullopt (with the reason in *why) on any I/O
// failure or undecodable reply; never throws for network problems.
[[nodiscard]] std::optional<Message> exchange(const Fd& fd, const Message& request,
                                              Deadline deadline, IoStatus* why = nullptr);

// Connect + exchange + close: for one-off calls (the CLI client, tests).
[[nodiscard]] std::optional<Message> call(const std::string& host, std::uint16_t port,
                                          const Message& request, Deadline deadline,
                                          IoStatus* why = nullptr);

}  // namespace gaffer::net
