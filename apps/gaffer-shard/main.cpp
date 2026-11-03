// gaffer-shard: serves one shard of the index over TCP.
//
//   gaffer-shard --corpus shard-0.tsv --index shard-0.gaffer --shard-id 0 --num-shards 4
//                [--host 0.0.0.0] [--port 7001] [--threads 4] [--nicknames data/nicknames.tsv]
//                [--delay-ms 0]
//
// Files come from `gaffer partition`. Stops cleanly on SIGINT/SIGTERM.

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
#include "corpus/corpus.hpp"
#include "index/index_file.hpp"
#include "query/nicknames.hpp"
#include "server/shard_server.hpp"

namespace {

struct Args {
  std::string corpus;
  std::string index;
  std::string nicknames = "data/nicknames.tsv";
  gaffer::server::ShardServerOptions server;
};

Args parse(std::span<char*> argv) {
  Args a;
  a.server.tcp.host = "0.0.0.0";
  a.server.tcp.port = 7001;
  for (std::size_t i = 1; i < argv.size(); ++i) {
    const std::string_view f = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argv.size()) {
        throw gaffer::Error(std::format("{} needs a value", f));
      }
      return argv[++i];
    };
    if (f == "--corpus") {
      a.corpus = value();
    } else if (f == "--index") {
      a.index = value();
    } else if (f == "--nicknames") {
      a.nicknames = value();
    } else if (f == "--host") {
      a.server.tcp.host = value();
    } else if (f == "--port") {
      a.server.tcp.port = static_cast<std::uint16_t>(std::stoul(value()));
    } else if (f == "--shard-id") {
      a.server.shard_id = static_cast<std::uint32_t>(std::stoul(value()));
    } else if (f == "--num-shards") {
      a.server.num_shards = static_cast<std::uint32_t>(std::stoul(value()));
    } else if (f == "--threads") {
      a.server.tcp.worker_threads = std::stoul(value());
    } else if (f == "--delay-ms") {
      a.server.search_delay = std::chrono::milliseconds(std::stoul(value()));
    } else {
      throw gaffer::Error(std::format("unknown option '{}'", f));
    }
  }
  if (a.corpus.empty() || a.index.empty()) {
    throw gaffer::Error("--corpus and --index are required");
  }
  return a;
}

int run(Args args) {
  // Block SIGINT/SIGTERM in every thread (the mask is inherited by threads
  // started after this), then wait for them synchronously below. Shutdown
  // code runs as normal code, not inside an async-signal handler.
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &signals, nullptr);

  auto corpus = gaffer::corpus::load_tsv_file(args.corpus);
  auto loaded = gaffer::index::load_index_file(args.index);
  if (loaded.corpus_fingerprint != gaffer::corpus::fingerprint(corpus)) {
    throw gaffer::Error("index '" + args.index + "' doesn't belong to corpus '" + args.corpus +
                        "'");
  }
  gaffer::query::NicknameExpander expander;
  if (std::filesystem::exists(args.nicknames)) {
    expander = gaffer::query::NicknameExpander::load_tsv_file(args.nicknames);
  }
  const auto docs = corpus.size();
  gaffer::server::ShardServer server(std::move(corpus), std::move(loaded.index),
                                     std::move(expander), args.server);
  server.start();
  std::cerr << std::format("gaffer-shard {}/{}: {} docs, listening on {}:{}\n",
                           args.server.shard_id, args.server.num_shards, docs, args.server.tcp.host,
                           server.port());
  int sig = 0;
  sigwait(&signals, &sig);
  std::cerr << std::format("gaffer-shard {}: signal {}, shutting down after {} requests\n",
                           args.server.shard_id, sig, server.requests_served());
  server.stop();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(parse(std::span<char*>(argv, static_cast<std::size_t>(argc))));
  } catch (const std::exception& e) {
    std::cerr << "gaffer-shard: " << e.what() << "\n";
    return 1;
  }
}
