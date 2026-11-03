#include "net/rpc.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include "common/error.hpp"
#include "net/framing.hpp"
#include "net/messages.hpp"
#include "net/socket.hpp"

namespace gaffer::net {

std::optional<Message> exchange(const Fd& fd, const Message& request, Deadline deadline,
                                IoStatus* why) {
  auto fail = [&](IoStatus s) -> std::optional<Message> {
    if (why != nullptr) {
      *why = s;
    }
    return std::nullopt;
  };
  if (const IoStatus w = write_frame(fd, encode(request), deadline); w != IoStatus::kOk) {
    return fail(w);
  }
  FrameResult f = read_frame(fd, deadline);
  if (f.status != IoStatus::kOk) {
    return fail(f.status);
  }
  try {
    Message m = decode(f.payload);
    if (why != nullptr) {
      *why = IoStatus::kOk;
    }
    return m;
  } catch (const FormatError&) {
    return fail(IoStatus::kError);
  }
}

std::optional<Message> call(const std::string& host, std::uint16_t port, const Message& request,
                            Deadline deadline, IoStatus* why) {
  const Fd fd = connect_tcp(host, port, deadline, why);
  if (!fd.valid()) {
    return std::nullopt;
  }
  return exchange(fd, request, deadline, why);
}

}  // namespace gaffer::net
