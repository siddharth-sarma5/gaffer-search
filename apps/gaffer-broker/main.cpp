// gaffer-broker: the front-end of a sharded Ask the Gaffer cluster.
//
//   gaffer-broker --shard HOST:PORT [--shard HOST:PORT ...] [--host 0.0.0.0] [--port 7000]
//                 [--deadline-ms 200] [--startup-timeout-ms 30000] [--threads 8]
//                 [--nicknames data/nicknames.tsv]
//
// Shards may be listed in any order: each reports its
// own id. On startup the broker gathers global statistics from all shards
// (retrying while they boot), then serves SearchRequests. Query it with
// `gaffer search --broker HOST:PORT "query"`. Stops on SIGINT/SIGTERM.

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include <pthread.h>

#include "common/error.hpp"
#include "query/nicknames.hpp"
#include "server/broker.hpp"

namespace {

struct Args {
  std::string nicknames = "data/nicknames.tsv";
  gaffer::server::BrokerOptions broker;
};

Args parse(std::span<char*> argv) {
  Args a;
  a.broker.tcp.host = "0.0.0.0";
  a.broker.tcp.port = 7000;
  a.broker.tcp.worker_threads = 8;
  for (std::size_t i = 1; i < argv.size(); ++i) {
    const std::string_view f = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argv.size()) {
        throw gaffer::Error(std::format("{} needs a value", f));
      }
      return argv[++i];
    };
    if (f == "--shard") {
      a.broker.shards.push_back(gaffer::server::parse_address(value()));
    } else if (f == "--host") {
      a.broker.tcp.host = value();
    } else if (f == "--port") {
      a.broker.tcp.port = static_cast<std::uint16_t>(std::stoul(value()));
    } else if (f == "--deadline-ms") {
      a.broker.shard_deadline = std::chrono::milliseconds(std::stoul(value()));
    } else if (f == "--startup-timeout-ms") {
      a.broker.startup_timeout = std::chrono::milliseconds(std::stoul(value()));
    } else if (f == "--threads") {
      a.broker.tcp.worker_threads = std::stoul(value());
    } else if (f == "--nicknames") {
      a.nicknames = value();
    } else {
      throw gaffer::Error(std::format("unknown option '{}'", f));
    }
  }
  if (a.broker.shards.empty()) {
    throw gaffer::Error("at least one --shard HOST:PORT is required");
  }
  return a;
}

int run(Args args) {
  sigset_t signals;  // see gaffer-shard: block now, sigwait below
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &signals, nullptr);

  gaffer::query::NicknameExpander expander;
  if (std::filesystem::exists(args.nicknames)) {
    expander = gaffer::query::NicknameExpander::load_tsv_file(args.nicknames);
  }
  const auto n = args.broker.shards.size();
  gaffer::server::Broker broker(std::move(expander), args.broker);
  broker.start();
  std::cerr << std::format(
      "gaffer-broker: {} shards, {} docs globally, deadline {} ms, listening on {}:{}\n", n,
      broker.global_stats().doc_count, args.broker.shard_deadline.count(), args.broker.tcp.host,
      broker.port());
  int sig = 0;
  sigwait(&signals, &sig);
  std::cerr << std::format("gaffer-broker: signal {}, shutting down\n", sig);
  broker.stop();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(parse(std::span<char*>(argv, static_cast<std::size_t>(argc))));
  } catch (const std::exception& e) {
    std::cerr << "gaffer-broker: " << e.what() << "\n";
    return 1;
  }
}
