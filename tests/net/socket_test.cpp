#include "net/socket.hpp"

#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sys/socket.h>

#include "common/error.hpp"

namespace gaffer::net {
namespace {

using namespace std::chrono_literals;

// A connected (client, server) pair over loopback.
std::pair<Fd, Fd> connected_pair() {
  const Fd listener = listen_tcp("127.0.0.1", 0);
  Fd client = connect_tcp("127.0.0.1", local_port(listener), Deadline::after(1s));
  Fd server = accept_until(listener, Deadline::after(1s));
  return {std::move(client), std::move(server)};
}

TEST(FdTest, MoveTransfersOwnershipAndResetCloses) {
  Fd a(::socket(AF_INET, SOCK_STREAM, 0));
  ASSERT_TRUE(a.valid());
  const int raw = a.get();
  Fd b(std::move(a));
  EXPECT_FALSE(a.valid());  // NOLINT(bugprone-use-after-move): checking the moved-from state
  EXPECT_EQ(b.get(), raw);
  b.reset();
  EXPECT_FALSE(b.valid());
}

TEST(SocketTest, ListenOnEphemeralPortThenConnect) {
  auto [client, server] = connected_pair();
  EXPECT_TRUE(client.valid());
  EXPECT_TRUE(server.valid());
}

TEST(SocketTest, ExactReadAcrossManySmallWrites) {
  auto [client, server] = connected_pair();
  // Writer sends 10 KiB in 1-byte writes on another thread; reader asks for it in one go.
  std::thread writer([fd = std::move(client)] {
    for (int i = 0; i < 10240; ++i) {
      const std::uint8_t b = static_cast<std::uint8_t>(i);
      ASSERT_EQ(write_all(fd, std::span(&b, 1), Deadline::after(5s)), IoStatus::kOk);
    }
  });
  std::vector<std::uint8_t> buf(10240);
  EXPECT_EQ(read_exact(server, buf, Deadline::after(5s)), IoStatus::kOk);
  writer.join();
  for (std::size_t i = 0; i < buf.size(); ++i) {
    ASSERT_EQ(buf[i], static_cast<std::uint8_t>(i)) << i;
  }
}

TEST(SocketTest, ReadTimesOutAtDeadlineWhenPeerIsSilent) {
  auto [client, server] = connected_pair();
  std::vector<std::uint8_t> buf(4);
  const auto t0 = Clock::now();
  EXPECT_EQ(read_exact(server, buf, Deadline::after(50ms)), IoStatus::kTimeout);
  const auto waited = Clock::now() - t0;
  EXPECT_GE(waited, 45ms);
  EXPECT_LT(waited, 1s);  // honoured the deadline, not "forever"
}

TEST(SocketTest, PartialDataThenSilenceStillTimesOut) {
  // A "slow loris" peer: sends 2 of 4 bytes and stops.
  auto [client, server] = connected_pair();
  const std::vector<std::uint8_t> two{1, 2};
  ASSERT_EQ(write_all(client, two, Deadline::after(1s)), IoStatus::kOk);
  std::vector<std::uint8_t> buf(4);
  EXPECT_EQ(read_exact(server, buf, Deadline::after(50ms)), IoStatus::kTimeout);
}

TEST(SocketTest, PeerCloseIsReportedNotCrashing) {
  auto [client, server] = connected_pair();
  client.reset();
  std::vector<std::uint8_t> buf(4);
  EXPECT_EQ(read_exact(server, buf, Deadline::after(1s)), IoStatus::kClosed);
  // Writing to a closed peer: kClosed (or kOk if the kernel buffered the first
  // write), but never SIGPIPE. Loop until the RST arrives.
  const std::vector<std::uint8_t> data(65536, 7);
  IoStatus s = IoStatus::kOk;
  for (int i = 0; i < 50 && s == IoStatus::kOk; ++i) {
    s = write_all(server, data, Deadline::after(1s));
  }
  EXPECT_EQ(s, IoStatus::kClosed);
}

TEST(SocketTest, ConnectToClosedPortFailsFast) {
  std::uint16_t port = 0;
  {
    const Fd l = listen_tcp("127.0.0.1", 0);
    port = local_port(l);
  }  // closed: nothing listens on `port` any more
  IoStatus why = IoStatus::kOk;
  const Fd fd = connect_tcp("127.0.0.1", port, Deadline::after(1s), &why);
  EXPECT_FALSE(fd.valid());
  EXPECT_NE(why, IoStatus::kOk);
}

TEST(SocketTest, AcceptTimesOut) {
  const Fd l = listen_tcp("127.0.0.1", 0);
  EXPECT_FALSE(accept_until(l, Deadline::after(20ms)).valid());
}

TEST(SocketTest, BindConflictThrows) {
  const Fd l = listen_tcp("127.0.0.1", 0);
  // SO_REUSEADDR doesn't allow two *listening* sockets on one port.
  EXPECT_THROW((void)listen_tcp("127.0.0.1", local_port(l)), IoError);
}

TEST(DeadlineTest, PollTimeout) {
  EXPECT_EQ(Deadline::never().poll_timeout_ms(), -1);
  EXPECT_EQ(Deadline{Clock::now() - 1s}.poll_timeout_ms(), 0);
  EXPECT_GT(Deadline::after(1s).poll_timeout_ms(), 900);
}

}  // namespace
}  // namespace gaffer::net
