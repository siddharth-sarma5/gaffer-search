#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "common/types.hpp"

namespace gaffer::net {

// Wire protocol, version 1. Every frame payload is:
//
//   u8 protocol version | u8 message type | body
//
// Body fields use: varint (unsigned LEB128) for integers, varint length +
// bytes for strings, 8 little-endian bytes for doubles (IEEE-754 bits, so
// scores cross the wire exactly; this is needed for the sharded == single-node
// equivalence test). A decoder rejects unknown versions and types, truncated
// or trailing bytes, and counts larger than the bytes left.
inline constexpr std::uint8_t kProtocolVersion = 1;

enum class MessageType : std::uint8_t {
  kStatsRequest = 1,
  kStatsResponse = 2,
  kShardSearchRequest = 3,
  kShardSearchResponse = 4,
  kSearchRequest = 5,
  kSearchResponse = 6,
  kError = 7,
};

using TermDf = std::pair<std::string, std::uint32_t>;

// Broker -> shard at startup: "describe your slice of the collection".
struct StatsRequest {
  friend bool operator==(const StatsRequest&, const StatsRequest&) = default;
};

// Shard -> broker: everything needed to compute *global* BM25 statistics.
struct StatsResponse {
  std::uint32_t shard_id = 0;
  std::uint32_t num_shards = 0;
  std::uint32_t doc_count = 0;
  std::uint64_t total_length = 0;
  std::uint32_t analyzer_flags = 0;  // same bits as the index file (stem, stopwords)
  std::vector<TermDf> df;            // every term in the shard with its local df

  friend bool operator==(const StatsResponse&, const StatsResponse&) = default;
};

// Broker -> shard per query: the query plus the global statistics for its
// terms, so the shard scores exactly as a single node would (D-020).
struct ShardSearchRequest {
  std::string query;
  std::uint32_t k = 10;
  std::uint32_t doc_count = 0;     // global N
  std::uint64_t total_length = 0;  // global sum of doc lengths (avgdl = total / N)
  std::vector<TermDf> df;          // global df for each term the query uses

  friend bool operator==(const ShardSearchRequest&, const ShardSearchRequest&) = default;
};

struct Hit {
  DocId doc = 0;  // global DocId: used for the deterministic tie-break when merging
  double score = 0.0;
  std::uint32_t shard = 0;
  std::string key;
  std::string title;
  std::string snippet;  // rendered with ** markers

  friend bool operator==(const Hit&, const Hit&) = default;
};

struct ShardSearchResponse {
  std::uint32_t shard_id = 0;
  std::uint64_t matched = 0;
  std::vector<Hit> hits;  // the shard's local top-k, best first

  friend bool operator==(const ShardSearchResponse&, const ShardSearchResponse&) = default;
};

// Client -> broker.
struct SearchRequest {
  std::string query;
  std::uint32_t k = 10;

  friend bool operator==(const SearchRequest&, const SearchRequest&) = default;
};

// Broker -> client.
struct SearchResponse {
  std::uint64_t matched = 0;  // summed over the shards that answered
  std::uint32_t shards_total = 0;
  std::vector<std::uint32_t> missing_shards;  // timed out or failed; non-empty = partial
  std::vector<Hit> hits;                      // global top-k, best first
  double broker_ms = 0.0;                     // time spent inside the broker

  [[nodiscard]] bool partial() const noexcept { return !missing_shards.empty(); }
  friend bool operator==(const SearchResponse&, const SearchResponse&) = default;
};

struct ErrorMessage {
  std::string message;

  friend bool operator==(const ErrorMessage&, const ErrorMessage&) = default;
};

using Message = std::variant<StatsRequest, StatsResponse, ShardSearchRequest, ShardSearchResponse,
                             SearchRequest, SearchResponse, ErrorMessage>;

[[nodiscard]] std::vector<std::uint8_t> encode(const Message& message);

// Throws gaffer::FormatError on any malformed input. Never reads out of bounds.
[[nodiscard]] Message decode(std::span<const std::uint8_t> bytes);

}  // namespace gaffer::net
