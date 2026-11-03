#include "server/broker.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "common/error.hpp"
#include "net/messages.hpp"
#include "net/rpc.hpp"
#include "net/socket.hpp"
#include "query/searcher.hpp"
#include "query/top_k.hpp"
#include "server/global_stats.hpp"

namespace gaffer::server {

namespace {

using namespace std::chrono_literals;

bool ranks_before(const net::Hit& a, const net::Hit& b) noexcept {
  return query::ranks_before({a.doc, a.score}, {b.doc, b.score});
}

}  // namespace

ShardAddress parse_address(const std::string& text) {
  const auto colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 == text.size()) {
    throw Error("expected host:port, got '" + text + "'");
  }
  try {
    const unsigned long port = std::stoul(text.substr(colon + 1));
    if (port == 0 || port > 65535) {
      throw Error("");
    }
    return {text.substr(0, colon), static_cast<std::uint16_t>(port)};
  } catch (const std::exception&) {
    throw Error("bad port in '" + text + "'");
  }
}

std::vector<net::Hit> merge_top_k(const std::vector<std::vector<net::Hit>>& lists, std::size_t k) {
  // Heap of (list index, position); the best head is on top.
  using Cursor = std::pair<std::size_t, std::size_t>;
  auto worse = [&](const Cursor& a, const Cursor& b) {
    return ranks_before(lists[b.first][b.second], lists[a.first][a.second]);
  };
  std::priority_queue<Cursor, std::vector<Cursor>, decltype(worse)> heads(worse);
  for (std::size_t i = 0; i < lists.size(); ++i) {
    if (!lists[i].empty()) {
      heads.emplace(i, 0);
    }
  }
  std::vector<net::Hit> out;
  while (out.size() < k && !heads.empty()) {
    const auto [list, pos] = heads.top();
    heads.pop();
    out.push_back(lists[list][pos]);
    if (pos + 1 < lists[list].size()) {
      heads.emplace(list, pos + 1);
    }
  }
  return out;
}

Broker::Broker(query::NicknameExpander expander, BrokerOptions options)
    : expander_(std::move(expander)),
      options_(std::move(options)),
      tcp_([this](const net::Message& m) { return handle(m); }, options_.tcp) {
  if (options_.shards.empty()) {
    throw Error("a broker needs at least one shard");
  }
  for (const auto& addr : options_.shards) {
    auto ch = std::make_unique<ShardChannel>();
    ch->address = addr;
    channels_.push_back(std::move(ch));
  }
}

Broker::~Broker() { stop(); }

void Broker::start() {
  gather_stats();
  tcp_.start();
}

void Broker::stop() { tcp_.stop(); }

void Broker::gather_stats() {
  const auto give_up = net::Deadline::after(options_.startup_timeout);
  std::vector<net::StatsResponse> stats(channels_.size());
  for (std::size_t i = 0; i < channels_.size(); ++i) {
    while (true) {
      const auto reply = call_shard(i, net::StatsRequest{}, net::Deadline::after(5s));
      if (reply && std::holds_alternative<net::StatsResponse>(*reply)) {
        stats[i] = std::get<net::StatsResponse>(*reply);
        break;
      }
      if (give_up.expired()) {
        const auto& a = channels_[i]->address;
        throw Error("shard " + a.host + ":" + std::to_string(a.port) + " unreachable at startup");
      }
      std::this_thread::sleep_for(200ms);  // shards may still be loading their index
    }
  }
  global_ = merge_stats(stats);
  analyzer_ = text::Analyzer{analyzer_options(stats.front().analyzer_flags)};
}

std::optional<net::Message> Broker::call_shard(std::size_t i, const net::Message& request,
                                               net::Deadline deadline) {
  ShardChannel& ch = *channels_[i];
  for (int attempt = 0; attempt < 2; ++attempt) {
    net::Fd conn;
    bool pooled = false;
    {
      const std::scoped_lock lock(ch.mu);
      if (!ch.idle.empty()) {
        conn = std::move(ch.idle.back());
        ch.idle.pop_back();
        pooled = true;
      }
    }
    if (!conn.valid()) {
      conn = net::connect_tcp(ch.address.host, ch.address.port, deadline);
      if (!conn.valid()) {
        return std::nullopt;
      }
    }
    net::IoStatus why = net::IoStatus::kOk;
    auto reply = net::exchange(conn, request, deadline, &why);
    if (reply) {
      const std::scoped_lock lock(ch.mu);
      ch.idle.push_back(std::move(conn));
      return reply;
    }
    // A pooled connection may simply be stale (the shard restarted since it
    // was opened): retry once on a fresh connection if there's time left.
    // A fresh connection failing, or a timeout, is a real failure.
    if (!pooled || why == net::IoStatus::kTimeout || deadline.expired()) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

net::SearchResponse Broker::search(const net::SearchRequest& request) {
  const auto t0 = std::chrono::steady_clock::now();
  const auto plan = query::plan_query(request.query, analyzer_, &expander_);
  const net::Message shard_request =
      net::ShardSearchRequest{request.query, request.k, global_.doc_count, global_.total_length,
                              df_for(global_, query::required_terms(plan))};

  const auto deadline = net::Deadline::after(options_.shard_deadline);
  std::vector<std::optional<net::Message>> replies(channels_.size());
  {
    std::vector<std::jthread> fan_out;
    fan_out.reserve(channels_.size());
    for (std::size_t i = 0; i < channels_.size(); ++i) {
      fan_out.emplace_back([&, i] { replies[i] = call_shard(i, shard_request, deadline); });
    }
  }  // jthreads join here: every reply slot is final

  net::SearchResponse response;
  response.shards_total = static_cast<std::uint32_t>(channels_.size());
  std::vector<std::vector<net::Hit>> lists;
  for (std::size_t i = 0; i < replies.size(); ++i) {
    const std::optional<net::Message>& reply = replies[i];
    const auto* r =
        reply.has_value() ? std::get_if<net::ShardSearchResponse>(&reply.value()) : nullptr;
    if (r == nullptr) {
      // Timed out, refused, hung up, or answered with an error: degrade
      // gracefully. The client gets the other shards' results, flagged as
      // partial with this shard listed, within the same deadline.
      response.missing_shards.push_back(static_cast<std::uint32_t>(i));
      continue;
    }
    response.matched += r->matched;
    lists.push_back(r->hits);
  }
  response.hits = merge_top_k(lists, request.k);
  response.broker_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return response;
}

net::Message Broker::handle(const net::Message& request) {
  return std::visit(
      [this](const auto& m) -> net::Message {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, net::SearchRequest>) {
          return search(m);
        } else {
          return net::ErrorMessage{"unsupported request for the broker"};
        }
      },
      request);
}

}  // namespace gaffer::server
