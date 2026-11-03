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

// A distinct analysed query term (from a free-words clause) and how often it
// appeared in the query.
struct QueryTerm {
  std::string term;
  std::uint32_t query_tf;
  std::uint32_t doc_freq;
  double idf;
};

// An analysed quoted phrase.
struct QueryPhrase {
  std::string text;                    // as typed, without quotes
  std::vector<std::string> terms;      // analysed words in order (may repeat)
  std::vector<std::uint32_t> offsets;  // position of each word relative to the first
  double idf;                          // sum of the words' idfs
};

struct SearchResult {
  std::vector<Clause> clauses;       // the parsed query
  std::vector<QueryTerm> terms;      // free words, in order of first appearance
  std::vector<QueryPhrase> phrases;  // quoted phrases, in query order
  std::vector<ScoredDoc> hits;       // best first, at most k
  std::size_t matched = 0;           // documents that satisfied the query
};

// Runs ranked queries against an index.
//
// Query semantics (docs/DECISIONS.md D-015):
//  * Free words are optional (OR): a document matches if it has any of them.
//  * Quoted phrases are required (AND): a document must contain every phrase,
//    words adjacent and in order. Stopword gaps count, so "stadium of light"
//    matches "Stadium of Light" but not "stadium light".
//  * With at least one phrase, only documents containing all phrases match;
//    free words then only add to the score.
//
// Scoring: BM25. Each free word contributes query_tf * bm25(idf, tf, len). Each
// phrase is scored as one pseudo-term, with tf = number of occurrences and
// idf = sum of its words' idfs (as Lucene does). Phrase words are not also
// scored individually, which would count them twice.
//
// Evaluation strategy: document-at-a-time (DAAT) over PostingCursors.
//  * No phrases: take the smallest current doc among the cursors, score it
//    completely, advance.
//  * With phrases: leapfrog-intersect the cursors of every phrase word with
//    advance_to (galloping), then check positions only for documents that
//    contain every word.
// Either way: O(#cursors + k) memory, and a doc's positions are all at hand.
//
// Non-owning: the index must outlive the Searcher.
class Searcher {
 public:
  explicit Searcher(const index::InvertedIndex& index, ranking::Bm25Params params = {});

  [[nodiscard]] SearchResult search(std::string_view query, std::size_t k) const;

  // The analysed, de-duplicated free-word terms of a query (phrases excluded),
  // with df and idf filled in.
  [[nodiscard]] std::vector<QueryTerm> query_terms(std::string_view query) const;

 private:
  void analyze_clauses(SearchResult& result) const;
  void search_or(SearchResult& result, TopK& top) const;
  void search_with_phrases(SearchResult& result, TopK& top) const;

  const index::InvertedIndex* index_;
  text::Analyzer analyzer_;
  ranking::Bm25 bm25_;
};

}  // namespace gaffer::query
