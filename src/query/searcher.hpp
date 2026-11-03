#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "index/inverted_index.hpp"
#include "query/top_k.hpp"
#include "ranking/bm25.hpp"
#include "text/analyzer.hpp"

namespace gaffer::query {

// A distinct analysed query term and how often it appeared in the query.
struct QueryTerm {
  std::string term;
  std::uint32_t query_tf;
  std::uint32_t doc_freq;
  double idf;
};

struct SearchResult {
  std::vector<QueryTerm> terms;  // in order of first appearance in the query
  std::vector<ScoredDoc> hits;   // best first, at most k
  std::size_t matched = 0;       // documents containing at least one query term
};

// Runs ranked disjunctive ("OR") queries against an index.
//
// M1 evaluation strategy: term-at-a-time (TAAT).
//   for each query term t:
//     for each posting (d, tf) in postings(t):
//       acc[d] += query_tf(t) * bm25(idf(t), tf, len(d))
//   then push every touched d into a size-k TopK.
// The accumulators are a dense array indexed by DocId plus a list of touched
// docs, so updates are O(1) array writes with no hashing. The cost is O(N)
// memory per query. Fine for thousands of docs. M2 replaces this with
// document-at-a-time evaluation, which needs no accumulators and supports phrases.
//
// Non-owning: the index must outlive the Searcher.
class Searcher {
 public:
  explicit Searcher(const index::InvertedIndex& index, ranking::Bm25Params params = {});

  [[nodiscard]] SearchResult search(std::string_view query, std::size_t k) const;

  // The analysed, de-duplicated terms of a query, with df and idf filled in.
  [[nodiscard]] std::vector<QueryTerm> query_terms(std::string_view query) const;

 private:
  const index::InvertedIndex* index_;
  text::Analyzer analyzer_;
  ranking::Bm25 bm25_;
};

}  // namespace gaffer::query
