#include "net/messages.hpp"

#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "common/error.hpp"
#include "index/varint.hpp"

namespace gaffer::net {

namespace {

using index::put_varint;
using index::VarintReader;

// --- writer ----------------------------------------------------------------

class Writer {
 public:
  explicit Writer(MessageType type) {
    out_.push_back(kProtocolVersion);
    out_.push_back(static_cast<std::uint8_t>(type));
  }
  void u(std::uint64_t v) { put_varint(out_, v); }
  void str(const std::string& s) {
    u(s.size());
    out_.insert(out_.end(), s.begin(), s.end());
  }
  void f64(double d) {
    const auto bits = std::bit_cast<std::uint64_t>(d);
    for (unsigned shift = 0; shift < 64; shift += 8) {
      out_.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
  }
  void dfs(const std::vector<TermDf>& df) {
    u(df.size());
    for (const auto& [term, n] : df) {
      str(term);
      u(n);
    }
  }
  void hits(const std::vector<Hit>& hs) {
    u(hs.size());
    for (const Hit& h : hs) {
      u(h.doc);
      f64(h.score);
      u(h.shard);
      str(h.key);
      str(h.title);
      str(h.snippet);
    }
  }
  std::vector<std::uint8_t> take() { return std::move(out_); }

 private:
  std::vector<std::uint8_t> out_;
};

// --- reader ----------------------------------------------------------------

[[noreturn]] void bad(const std::string& what) { throw FormatError("bad message: " + what); }

class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> b) : in_(b) {}

  std::uint64_t u64() {
    const auto v = in_.read_u64();
    if (!v) {
      bad("bad varint");
    }
    return *v;
  }
  std::uint32_t u32() {
    const auto v = in_.read_u32();
    if (!v) {
      bad("bad varint");
    }
    return *v;
  }
  // A count of items that each need at least `min_item_bytes`: bounded by the
  // bytes left before anything is allocated.
  std::uint64_t count(std::size_t min_item_bytes) {
    const std::uint64_t n = u64();
    if (n > in_.remaining() / min_item_bytes) {
      bad("count larger than the message");
    }
    return n;
  }
  std::string str() {
    const std::uint64_t n = u64();
    if (n > in_.remaining()) {
      bad("string runs past the end");
    }
    const auto b = in_.data().subspan(in_.position(), n);
    in_.skip(n);
    return {b.begin(), b.end()};
  }
  double f64() {
    if (in_.remaining() < 8) {
      bad("truncated double");
    }
    std::uint64_t bits = 0;
    const auto b = in_.data().subspan(in_.position(), 8);
    for (unsigned i = 0; i < 8; ++i) {
      bits |= static_cast<std::uint64_t>(b[i]) << (8U * i);
    }
    in_.skip(8);
    return std::bit_cast<double>(bits);
  }
  std::vector<TermDf> dfs() {
    std::vector<TermDf> out(count(2));
    for (auto& [term, n] : out) {
      term = str();
      n = u32();
    }
    return out;
  }
  std::vector<Hit> hits() {
    std::vector<Hit> out(count(13));  // doc, 8-byte score, shard, 3 string lengths
    for (Hit& h : out) {
      h.doc = u32();
      h.score = f64();
      h.shard = u32();
      h.key = str();
      h.title = str();
      h.snippet = str();
    }
    return out;
  }
  void finish() {
    if (!in_.at_end()) {
      bad("trailing bytes");
    }
  }

 private:
  VarintReader in_;
};

}  // namespace

std::vector<std::uint8_t> encode(const Message& message) {
  return std::visit(
      [](const auto& m) -> std::vector<std::uint8_t> {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, StatsRequest>) {
          return Writer(MessageType::kStatsRequest).take();
        } else if constexpr (std::is_same_v<T, StatsResponse>) {
          Writer w(MessageType::kStatsResponse);
          w.u(m.shard_id);
          w.u(m.num_shards);
          w.u(m.doc_count);
          w.u(m.total_length);
          w.u(m.analyzer_flags);
          w.dfs(m.df);
          return w.take();
        } else if constexpr (std::is_same_v<T, ShardSearchRequest>) {
          Writer w(MessageType::kShardSearchRequest);
          w.str(m.query);
          w.u(m.k);
          w.u(m.doc_count);
          w.u(m.total_length);
          w.dfs(m.df);
          return w.take();
        } else if constexpr (std::is_same_v<T, ShardSearchResponse>) {
          Writer w(MessageType::kShardSearchResponse);
          w.u(m.shard_id);
          w.u(m.matched);
          w.hits(m.hits);
          return w.take();
        } else if constexpr (std::is_same_v<T, SearchRequest>) {
          Writer w(MessageType::kSearchRequest);
          w.str(m.query);
          w.u(m.k);
          return w.take();
        } else if constexpr (std::is_same_v<T, SearchResponse>) {
          Writer w(MessageType::kSearchResponse);
          w.u(m.matched);
          w.u(m.shards_total);
          w.u(m.missing_shards.size());
          for (const auto s : m.missing_shards) {
            w.u(s);
          }
          w.hits(m.hits);
          w.f64(m.broker_ms);
          return w.take();
        } else {
          static_assert(std::is_same_v<T, ErrorMessage>);
          Writer w(MessageType::kError);
          w.str(m.message);
          return w.take();
        }
      },
      message);
}

Message decode(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 2) {
    bad("too short");
  }
  if (bytes[0] != kProtocolVersion) {
    bad("unsupported protocol version " + std::to_string(bytes[0]));
  }
  Reader r(bytes.subspan(2));
  Message out;
  switch (static_cast<MessageType>(bytes[1])) {
    case MessageType::kStatsRequest:
      out = StatsRequest{};
      break;
    case MessageType::kStatsResponse: {
      StatsResponse m;
      m.shard_id = r.u32();
      m.num_shards = r.u32();
      m.doc_count = r.u32();
      m.total_length = r.u64();
      m.analyzer_flags = r.u32();
      m.df = r.dfs();
      out = std::move(m);
      break;
    }
    case MessageType::kShardSearchRequest: {
      ShardSearchRequest m;
      m.query = r.str();
      m.k = r.u32();
      m.doc_count = r.u32();
      m.total_length = r.u64();
      m.df = r.dfs();
      out = std::move(m);
      break;
    }
    case MessageType::kShardSearchResponse: {
      ShardSearchResponse m;
      m.shard_id = r.u32();
      m.matched = r.u64();
      m.hits = r.hits();
      out = std::move(m);
      break;
    }
    case MessageType::kSearchRequest: {
      SearchRequest m;
      m.query = r.str();
      m.k = r.u32();
      out = std::move(m);
      break;
    }
    case MessageType::kSearchResponse: {
      SearchResponse m;
      m.matched = r.u64();
      m.shards_total = r.u32();
      m.missing_shards.resize(r.count(1));
      for (auto& s : m.missing_shards) {
        s = r.u32();
      }
      m.hits = r.hits();
      m.broker_ms = r.f64();
      out = std::move(m);
      break;
    }
    case MessageType::kError:
      out = ErrorMessage{r.str()};
      break;
    default:
      bad("unknown message type " + std::to_string(bytes[1]));
  }
  r.finish();
  return out;
}

}  // namespace gaffer::net
