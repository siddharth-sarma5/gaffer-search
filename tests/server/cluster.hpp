#pragma once

// Test helper: an in-process cluster of N ShardServers plus a Broker, all on
// ephemeral localhost ports, with real TCP between them.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "index/partition.hpp"
#include "query/nicknames.hpp"
#include "server/broker.hpp"
#include "server/shard_server.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server::testing {

struct ClusterOptions {
  std::chrono::milliseconds shard_deadline{2000};
  // Per-shard artificial delay (fault injection); missing entries = 0.
  std::vector<std::chrono::milliseconds> delays;
};

class Cluster {
 public:
  Cluster(const corpus::Corpus& corpus, std::uint32_t num_shards,
          const query::NicknameExpander& expander, ClusterOptions options = {}) {
    auto parts = index::partition(corpus, num_shards);
    for (std::uint32_t s = 0; s < num_shards; ++s) {
      auto idx = index::build_index(parts[s], text::Analyzer{});
      ShardServerOptions so{.shard_id = s, .num_shards = num_shards, .tcp = {}};
      so.tcp.worker_threads = 4;
      if (s < options.delays.size()) {
        so.search_delay = options.delays[s];
      }
      shards.push_back(
          std::make_unique<ShardServer>(std::move(parts[s]), std::move(idx), expander, so));
      shards.back()->start();
    }
    BrokerOptions bo;
    bo.shard_deadline = options.shard_deadline;
    bo.startup_timeout = std::chrono::milliseconds(5000);
    for (const auto& s : shards) {
      bo.shards.push_back({"127.0.0.1", s->port()});
    }
    broker = std::make_unique<Broker>(expander, bo);
    broker->start();
  }

  ~Cluster() {
    if (broker) {
      broker->stop();
    }
    for (auto& s : shards) {
      s->stop();
    }
  }
  Cluster(const Cluster&) = delete;
  Cluster& operator=(const Cluster&) = delete;
  Cluster(Cluster&&) = delete;
  Cluster& operator=(Cluster&&) = delete;

  std::vector<std::unique_ptr<ShardServer>> shards;
  std::unique_ptr<Broker> broker;
};

}  // namespace gaffer::server::testing
