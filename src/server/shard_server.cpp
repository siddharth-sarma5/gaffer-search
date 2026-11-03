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
#include "snippet/snippet.hpp"

namespace gaffer::server {

namespace {

using namespace std::chrono_literals;
constexpr auto kPollSlice = 100ms;  // how often blocked threads re-check their stop_token

}  // namespace

ShardServer::ShardServer(corpus::Corpus corpus, index::InvertedIndex index,
                         query::NicknameExpander expander, ShardServerOptions options)
    : corpus_(std::move(corpus)),
      index_(std::move(index)),
      expander_(std::move(expander)),
      options_(std::move(options)),
      // Built after index_/expander_ (declaration order) and pointing at the
      // members, never at the constructor arguments, which have been moved from.
      searcher_(index_, {}, &expander_),
      analyzer_(index_.analyzer_options()) {
  if (corpus_.size() != index_.stats().doc_count) {
    throw Error("shard corpus and index disagree on the number of documents");
  }
  if (options_.num_shards == 0 || options_.shard_id >= options_.num_shards) {
    throw Error("invalid shard id / shard count");
  }
}

ShardServer::~ShardServer() { stop(); }

void ShardServer::start() {
  listener_ = net::listen_tcp(options_.host, options_.port);
  port_ = net::local_port(listener_);
  threads_.emplace_back([this](const std::stop_token& st) { accept_loop(st); });
  for (std::size_t i = 0; i < options_.worker_threads; ++i) {
    threads_.emplace_back([this](const std::stop_token& st) { worker_loop(st); });
  }
}

void ShardServer::stop() {
  for (auto& t : threads_) {
    t.request_stop();
  }
  cv_.notify_all();
  threads_.clear();  // jthread destructors join
  const std::scoped_lock lock(mu_);
  queue_.clear();  // close connections that were never served
  listener_.reset();
}

void ShardServer::accept_loop(const std::stop_token& stop) {
  while (!stop.stop_requested()) {
    net::Fd conn = net::accept_until(listener_, net::Deadline::after(kPollSlice));
    if (!conn.valid()) {
      continue;  // timeout slice (or a transient accept error): re-check stop
    }
    {
      const std::scoped_lock lock(mu_);
      if (queue_.size() >= options_.max_queued_connections) {
        continue;  // back-pressure: conn closes as it goes out of scope
      }
      queue_.push_back(std::move(conn));
    }
    cv_.notify_one();
  }
}

void ShardServer::worker_loop(const std::stop_token& stop) {
  while (!stop.stop_requested()) {
    net::Fd conn;
    {
      std::unique_lock lock(mu_);
      // condition_variable_any + stop_token: the wait also wakes on stop.
      if (!cv_.wait(lock, stop, [this] { return !queue_.empty(); })) {
        return;
      }
      conn = std::move(queue_.front());
      queue_.pop_front();
    }
    serve(conn, stop);
  }
}

void ShardServer::serve(const net::Fd& conn, const std::stop_token& stop) {
  while (!stop.stop_requested()) {
    // Idle wait in short slices so shutdown isn't blocked by an idle client;
    // nothing is consumed until a request actually starts arriving.
    const net::IoStatus ready = net::wait_readable(conn, net::Deadline::after(kPollSlice));
    if (ready == net::IoStatus::kTimeout) {
      continue;
    }
    if (ready != net::IoStatus::kOk) {
      return;
    }
    const auto deadline = net::Deadline::after(options_.io_timeout);
    net::FrameResult frame = net::read_frame(conn, deadline);
    if (frame.status != net::IoStatus::kOk) {
      return;  // closed, timed out mid-frame, or oversized: drop the connection
    }
    net::Message reply;
    try {
      reply = handle(net::decode(frame.payload));
    } catch (const FormatError& e) {
      // Garbage in: answer with an error, then hang up. Framing is intact
      // (we read a whole frame), but this peer is speaking nonsense.
      (void)net::write_frame(conn, net::encode(net::ErrorMessage{e.what()}), deadline);
      return;
    }
    if (net::write_frame(conn, net::encode(reply), deadline) != net::IoStatus::kOk) {
      return;
    }
    served_.fetch_add(1, std::memory_order_relaxed);
  }
}

net::Message ShardServer::handle(const net::Message& request) const {
  return std::visit(
      [this](const auto& m) -> net::Message {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, net::ShardSearchRequest>) {
          return search(m);
        } else {
          return net::ErrorMessage{"unsupported request for a shard"};
        }
      },
      request);
}

net::Message ShardServer::search(const net::ShardSearchRequest& req) const {
  if (options_.search_delay.count() > 0) {
    // Fault injection only. A plain sleep: stop() may wait up to one delay
    // for an in-flight request, which is fine for tests and benchmarks.
    std::this_thread::sleep_for(options_.search_delay);
  }
  const query::SearchResult r = searcher_.search(req.query, req.k);
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
