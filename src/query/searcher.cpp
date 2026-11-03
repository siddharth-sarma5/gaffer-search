#include "query/searcher.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

#include "common/types.hpp"
#include "index/inverted_index.hpp"
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

  std::vector<double> acc(index_->stats().doc_count, 0.0);
  std::vector<DocId> touched;

  for (const QueryTerm& qt : result.terms) {
    // Repeating a word in the query ("salah salah") weights it linearly.
    const auto weight = static_cast<double>(qt.query_tf);
    for (const index::Posting& p : index_->postings(qt.term)) {
      if (acc[p.doc] == 0.0) {
        touched.push_back(p.doc);  // first contribution: BM25 terms are always > 0
      }
      acc[p.doc] += weight * bm25_.score(qt.idf, p.tf, index_->doc_length(p.doc));
    }
  }

  result.matched = touched.size();
  TopK top(k);
  for (const DocId d : touched) {
    top.push(ScoredDoc{d, acc[d]});
  }
  result.hits = std::move(top).take_sorted();
  return result;
}

}  // namespace gaffer::query
