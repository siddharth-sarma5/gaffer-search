#include "server/global_stats.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "common/error.hpp"
#include "index/index_file.hpp"
#include "net/messages.hpp"
#include "query/searcher.hpp"
#include "text/analyzer.hpp"

namespace gaffer::server {

std::uint32_t analyzer_flags(const text::AnalyzerOptions& options) noexcept {
  return (options.stem ? index::kFlagStem : 0U) |
         (options.remove_stopwords ? index::kFlagStopwords : 0U);
}

text::AnalyzerOptions analyzer_options(std::uint32_t flags) noexcept {
  return {.remove_stopwords = (flags & index::kFlagStopwords) != 0,
          .stem = (flags & index::kFlagStem) != 0};
}

query::GlobalStats merge_stats(const std::vector<net::StatsResponse>& shards) {
  if (shards.empty()) {
    throw Error("no shard statistics to merge");
  }
  const auto n = static_cast<std::uint32_t>(shards.size());
  std::vector<bool> seen(n, false);
  query::GlobalStats g;
  for (const auto& s : shards) {
    if (s.num_shards != n) {
      throw Error("shard " + std::to_string(s.shard_id) + " says the cluster has " +
                  std::to_string(s.num_shards) + " shards, but " + std::to_string(n) +
                  " responded");
    }
    if (s.shard_id >= n || seen[s.shard_id]) {
      throw Error("duplicate or out-of-range shard id " + std::to_string(s.shard_id));
    }
    seen[s.shard_id] = true;
    if (s.analyzer_flags != shards.front().analyzer_flags) {
      throw Error("shards were indexed with different analyzer settings");
    }
    g.doc_count += s.doc_count;
    g.total_length += s.total_length;
    for (const auto& [term, df] : s.df) {
      g.df[term] += df;
    }
  }
  return g;
}

std::vector<net::TermDf> df_for(const query::GlobalStats& stats,
                                const std::vector<std::string>& terms) {
  std::vector<net::TermDf> out;
  out.reserve(terms.size());
  for (const auto& t : terms) {
    out.emplace_back(t, stats.doc_freq(t));
  }
  return out;
}

}  // namespace gaffer::server
