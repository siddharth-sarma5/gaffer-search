#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/messages.hpp"
#include "query/searcher.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {

// Analyzer settings as the bit flags shared with the index file format.
[[nodiscard]] std::uint32_t analyzer_flags(const text::AnalyzerOptions& options) noexcept;
[[nodiscard]] text::AnalyzerOptions analyzer_options(std::uint32_t flags) noexcept;

// Combines every shard's StatsResponse into global statistics:
//   N = sum of doc counts, total length = sum, df(t) = sum of per-shard df(t).
// Summing is exact because a document lives on exactly one shard (doc
// partitioning), so no document is counted twice.
//
// Throws gaffer::Error unless the responses describe one consistent cluster:
// shard ids 0..N-1 each exactly once, all reporting the same N, and the same
// analyzer settings (mixing stemmed and unstemmed shards would make the same
// query mean different things on different shards).
[[nodiscard]] query::GlobalStats merge_stats(const std::vector<net::StatsResponse>& shards);

// The global df entries a request needs, in `terms` order (0 for unknown terms).
[[nodiscard]] std::vector<net::TermDf> df_for(const query::GlobalStats& stats,
                                              const std::vector<std::string>& terms);

}  // namespace gaffer::server
