#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "index/inverted_index.hpp"
#include "query/nicknames.hpp"
#include "query/query_parser.hpp"
#include "query/top_k.hpp"
#include "ranking/bm25.hpp"
#include "ranking/explanation.hpp"
#include "text/analyzer.hpp"

namespace gaffer::query {

// A distinct analysed query term (from a free-words clause or a nickname
// expansion) and how often it appeared in the query.
struct QueryTerm {
  std::string term;
  std::uint32_t query_tf;
  std::uint32_t doc_freq;
  double idf;
  double weight = 1.0;        // multiplier on its BM25 contribution (< 1 for expansions)
  std::string expanded_from;  // the alias it came from ("gunners"), empty if typed
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
  // The collection statistics the scores were computed with: the index's own
  // or the global ones supplied by a broker. explain() reuses them, so a VAR
  // review on a shard reproduces the shard's (globally comparable) score.
  std::uint32_t stats_doc_count = 0;
  double stats_avg_doc_length = 0.0;
};

// Collection statistics from outside the index: in the sharded deployment the
// broker sends the *global* N, total length and df of the query's terms with
// every request, so a shard computes exactly the BM25 scores a single node
// over the whole corpus would (D-014, D-020).
struct GlobalStats {
  std::uint32_t doc_count = 0;
  std::uint64_t total_length = 0;
  std::unordered_map<std::string, std::uint32_t, index::StringHash, std::equal_to<>> df;

  // Same formula as CollectionStats, so global and single-node avgdl are
  // bit-identical when the totals are.
  [[nodiscard]] double avg_doc_length() const noexcept {
    return index::CollectionStats{doc_count, total_length}.avg_doc_length();
  }
  [[nodiscard]] std::uint32_t doc_freq(std::string_view term) const {
    const auto it = df.find(term);
    return it == df.end() ? 0 : it->second;
  }
};

// The index-free half of query processing: parse, analyse and expand a query
// into its terms and phrases (doc_freq and idf left at 0). The broker uses it
// to learn which terms' global dfs a query needs without having an index.
[[nodiscard]] SearchResult plan_query(std::string_view query, const text::Analyzer& analyzer,
                                      const NicknameExpander* expander);

// Every analysed term a query uses (free words, expansions, phrase words),
// de-duplicated: the df entries a broker must send for it.
[[nodiscard]] std::vector<std::string> required_terms(const SearchResult& plan);

// Every analysed term worth highlighting in a result: free words and phrase
// words, de-duplicated, in query order.
[[nodiscard]] std::vector<std::string> highlight_terms(const SearchResult& result);

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
// Scoring: BM25. Each free word contributes query_tf * weight * bm25(idf, tf,
// len), where weight is 1 for typed words and kExpansionWeight for nickname
// expansions. Each
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
  // `expander` is optional; when given, free words that are club nicknames add
  // the club's name as extra terms weighted kExpansionWeight. Non-owning.
  explicit Searcher(const index::InvertedIndex& index, ranking::Bm25Params params = {},
                    const NicknameExpander* expander = nullptr);

  // With `global`, scores use those statistics instead of the index's own
  // (sharded mode). nullptr = single-node mode.
  [[nodiscard]] SearchResult search(std::string_view query, std::size_t k,
                                    const GlobalStats* global = nullptr) const;

  // "VAR review": recomputes `doc`'s score for the query in `result`, one term
  // or phrase at a time. Contributions are summed in the same order search()
  // uses, so explanation.total == the ranked score exactly. Terms that don't
  // occur in the doc are listed with tf 0 and score 0.
  [[nodiscard]] ranking::Explanation explain(const SearchResult& result, DocId doc) const;

  // The analysed, de-duplicated free-word terms of a query (phrases excluded),
  // with df and idf filled in.
  [[nodiscard]] std::vector<QueryTerm> query_terms(std::string_view query) const;

 private:
  void resolve_stats(SearchResult& result, const GlobalStats* global) const;
  void search_or(SearchResult& result, TopK& top, const ranking::Bm25& bm25) const;
  void search_with_phrases(SearchResult& result, TopK& top, const ranking::Bm25& bm25) const;
  [[nodiscard]] ranking::Bm25 bm25_for(const SearchResult& result) const;

  const index::InvertedIndex* index_;
  const NicknameExpander* expander_;
  text::Analyzer analyzer_;
  ranking::Bm25Params params_;
};

}  // namespace gaffer::query
