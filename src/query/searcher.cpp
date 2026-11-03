#include "query/searcher.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "common/types.hpp"
#include "index/inverted_index.hpp"
#include "index/posting_cursor.hpp"
#include "query/top_k.hpp"
#include "ranking/bm25.hpp"
#include "text/analyzer.hpp"

namespace gaffer::query {

Searcher::Searcher(const index::InvertedIndex& index, ranking::Bm25Params params)
    : index_(&index),
      // Same analysis as the index, or query terms won't meet document terms.
      analyzer_(index.analyzer_options()),
      bm25_(params, index.stats().doc_count, index.stats().avg_doc_length()) {}

std::vector<QueryTerm> Searcher::query_terms(std::string_view query) const {
  std::vector<QueryTerm> terms;
  for (const auto& tok : analyzer_.analyze(query)) {
    // Queries are a handful of terms, so a linear scan beats a hash set.
    const auto it = std::ranges::find(terms, tok.term, &QueryTerm::term);
    if (it != terms.end()) {
      ++it->query_tf;
      continue;
    }
    const std::uint32_t df = index_->doc_freq(tok.term);
    terms.push_back(QueryTerm{tok.term, 1, df, bm25_.idf(df)});
  }
  return terms;
}

SearchResult Searcher::search(std::string_view query, std::size_t k) const {
  SearchResult result;
  result.terms = query_terms(query);

  // One cursor per distinct query term, in query order.
  std::vector<index::PostingCursor> cursors;
  cursors.reserve(result.terms.size());
  for (const QueryTerm& qt : result.terms) {
    cursors.emplace_back(index_->posting_list(qt.term));
  }

  TopK top(k);
  while (true) {
    // With a handful of query terms, a linear scan for the minimum beats a heap.
    DocId doc = index::kNoMoreDocs;
    for (const auto& c : cursors) {
      doc = std::min(doc, c.doc());
    }
    if (doc == index::kNoMoreDocs) {
      break;
    }

    double score = 0.0;
    const std::uint32_t doc_length = index_->doc_length(doc);
    for (std::size_t i = 0; i < cursors.size(); ++i) {
      if (cursors[i].doc() != doc) {
        continue;
      }
      // Repeating a word in the query ("salah salah") weights it linearly.
      const auto weight = static_cast<double>(result.terms[i].query_tf);
      score += weight * bm25_.score(result.terms[i].idf, cursors[i].tf(), doc_length);
      cursors[i].next();
    }
    ++result.matched;
    top.push(ScoredDoc{doc, score});
  }
  result.hits = std::move(top).take_sorted();
  return result;
}

}  // namespace gaffer::query
