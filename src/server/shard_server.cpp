#include "server/shard_server.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include "common/error.hpp"
#include "index/partition.hpp"
#include "net/framing.hpp"
#include "net/messages.hpp"
#include "net/socket.hpp"
#include "query/searcher.hpp"
#include "server/global_stats.hpp"
#include "snippet/snippet.hpp"

namespace gaffer::server {

ShardServer::ShardServer(corpus::Corpus corpus, index::InvertedIndex index,
                         query::NicknameExpander expander, ShardServerOptions options)
    : corpus_(std::move(corpus)),
      index_(std::move(index)),
      expander_(std::move(expander)),
      options_(std::move(options)),
      // Built after index_/expander_ (declaration order) and pointing at the
      // members, never at the constructor arguments, which have been moved from.
      searcher_(index_, {}, &expander_),
      analyzer_(index_.analyzer_options()),
      tcp_([this](const net::Message& m) { return handle(m); }, options_.tcp) {
  if (corpus_.size() != index_.stats().doc_count) {
    throw Error("shard corpus and index disagree on the number of documents");
  }
  if (options_.num_shards == 0 || options_.shard_id >= options_.num_shards) {
    throw Error("invalid shard id / shard count");
  }
}

ShardServer::~ShardServer() { stop(); }

void ShardServer::start() { tcp_.start(); }

void ShardServer::stop() { tcp_.stop(); }

net::Message ShardServer::handle(const net::Message& request) const {
  return std::visit(
      [this](const auto& m) -> net::Message {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, net::ShardSearchRequest>) {
          return search(m);
        } else if constexpr (std::is_same_v<T, net::StatsRequest>) {
          return stats();
        } else {
          return net::ErrorMessage{"unsupported request for a shard"};
        }
      },
      request);
}

net::Message ShardServer::stats() const {
  net::StatsResponse s;
  s.shard_id = options_.shard_id;
  s.num_shards = options_.num_shards;
  s.doc_count = index_.stats().doc_count;
  s.total_length = index_.stats().total_length;
  s.analyzer_flags = analyzer_flags(index_.analyzer_options());
  for (const auto term : index_.sorted_terms()) {
    s.df.emplace_back(std::string(term), index_.doc_freq(term));
  }
  return s;
}

net::Message ShardServer::search(const net::ShardSearchRequest& req) const {
  if (options_.search_delay.count() > 0) {
    // Fault injection only. A plain sleep: stop() may wait up to one delay
    // for an in-flight request, which is fine for tests and benchmarks.
    std::this_thread::sleep_for(options_.search_delay);
  }
  // Global statistics from the broker (doc_count > 0) make this shard's scores
  // identical to a single node's. Without them (direct queries, tests) the
  // shard scores with its local statistics, which aren't comparable across shards.
  query::GlobalStats global;
  if (req.doc_count > 0) {
    global.doc_count = req.doc_count;
    global.total_length = req.total_length;
    for (const auto& [term, df] : req.df) {
      global.df.emplace(term, df);
    }
  }
  const query::SearchResult r =
      searcher_.search(req.query, req.k, req.doc_count > 0 ? &global : nullptr);
  const auto highlight = query::highlight_terms(r);
  net::ShardSearchResponse resp;
  resp.shard_id = options_.shard_id;
  resp.matched = r.matched;
  for (const auto& hit : r.hits) {
    const corpus::Document& d = corpus_[hit.doc];
    resp.hits.push_back(
        net::Hit{index::global_of(hit.doc, options_.shard_id, options_.num_shards), hit.score,
                 options_.shard_id, d.key, d.title,
                 snippet::render(snippet::make_snippet(d.body, analyzer_, highlight), "**", "**")});
  }
  return resp;
}

}  // namespace gaffer::server
