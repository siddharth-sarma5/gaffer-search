#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "index/inverted_index.hpp"
#include "query/query_parser.hpp"
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
  std::vector<Clause> clauses;   // the parsed query
  std::vector<QueryTerm> terms;  // in order of first appearance in the query
  std::vector<ScoredDoc> hits;   // best first, at most k
  std::size_t matched = 0;       // documents containing at least one query term
};

// Runs ranked disjunctive ("OR") queries against an index.
//
// Evaluation strategy: document-at-a-time (DAAT).
//   open a PostingCursor per query term
//   loop:
//     d = smallest current doc among the cursors   (kNoMoreDocs when all done)
//     score(d) = sum over cursors sitting on d of query_tf * bm25(idf, tf, len(d))
//     push (d, score) into a size-k TopK; advance those cursors
// Each document is scored completely in one step, so:
//   * no accumulator array: O(#terms + k) memory instead of O(N) (M1's TAAT);
//   * all of d's postings, including positions, are available together,
//     which is what phrase matching needs;
//   * it streams over postings in order, so compressed lists can be decoded
//     on the fly (M2) and dynamic pruning such as WAND could be added later.
// Contributions are summed in query-term order, the same order TAAT used, so
// scores are bit-for-bit identical to M1. The golden test checks this.
//
// Non-owning: the index must outlive the Searcher.
class Searcher {
 public:
  explicit Searcher(const index::InvertedIndex& index, ranking::Bm25Params params = {});

  [[nodiscard]] SearchResult search(std::string_view query, std::size_t k) const;

  // The analysed, de-duplicated terms of a query, with df and idf filled in.
  [[nodiscard]] std::vector<QueryTerm> query_terms(std::string_view query) const;
  [[nodiscard]] std::vector<QueryTerm> query_terms(const std::vector<Clause>& clauses) const;

 private:
  // Analyses `text` and merges its terms into `terms` (de-duplicating).
  void add_terms(std::string_view text, std::vector<QueryTerm>& terms) const;

  const index::InvertedIndex* index_;
  text::Analyzer analyzer_;
  ranking::Bm25 bm25_;
};

}  // namespace gaffer::query
