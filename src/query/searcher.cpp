#include "query/searcher.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "common/types.hpp"
#include "index/inverted_index.hpp"
#include "index/posting_cursor.hpp"
#include "query/phrase.hpp"
#include "query/query_parser.hpp"
#include "query/top_k.hpp"
#include "ranking/bm25.hpp"
#include "text/analyzer.hpp"

namespace gaffer::query {

namespace {

using index::kNoMoreDocs;
using index::PostingCursor;

// Leapfrog intersection step: moves every cursor to the smallest doc that all
// of them contain (at or after their current docs). Returns that doc, or
// kNoMoreDocs once any cursor runs out.
//
// Each pass either confirms that all cursors agree or raises the target to a
// larger doc, so it terminates. Skipping uses galloping advance_to, so rare
// words drive the intersection and common words are skipped through quickly.
DocId align(std::vector<PostingCursor>& cursors) noexcept {
  DocId target = 0;
  for (const auto& c : cursors) {
    target = std::max(target, c.doc());
  }
  while (true) {
    bool raised = false;
    for (auto& c : cursors) {
      c.advance_to(target);
      if (c.doc() == kNoMoreDocs) {
        return kNoMoreDocs;
      }
      if (c.doc() > target) {
        target = c.doc();
        raised = true;
      }
    }
    if (!raised) {
      return target;
    }
  }
}

}  // namespace

Searcher::Searcher(const index::InvertedIndex& index, ranking::Bm25Params params)
    : index_(&index),
      // Same analysis as the index, or query terms won't meet document terms.
      analyzer_(index.analyzer_options()),
      bm25_(params, index.stats().doc_count, index.stats().avg_doc_length()) {}

void Searcher::analyze_clauses(SearchResult& result) const {
  for (const Clause& clause : result.clauses) {
    const auto tokens = analyzer_.analyze(clause.text);
    if (clause.kind == ClauseKind::kPhrase) {
      if (tokens.empty()) {
        continue;  // e.g. "\"the\"": nothing searchable, so no constraint
      }
      QueryPhrase phrase{clause.text, {}, {}, 0.0};
      for (const auto& tok : tokens) {
        phrase.terms.push_back(tok.term);
        phrase.offsets.push_back(tok.position - tokens.front().position);
        phrase.idf += bm25_.idf(index_->doc_freq(tok.term));
      }
      result.phrases.push_back(std::move(phrase));
      continue;
    }
    for (const auto& tok : tokens) {
      // Queries are a handful of terms, so a linear scan beats a hash set.
      const auto it = std::ranges::find(result.terms, tok.term, &QueryTerm::term);
      if (it != result.terms.end()) {
        ++it->query_tf;
        continue;
      }
      const std::uint32_t df = index_->doc_freq(tok.term);
      result.terms.push_back(QueryTerm{tok.term, 1, df, bm25_.idf(df)});
    }
  }
}

std::vector<QueryTerm> Searcher::query_terms(std::string_view query) const {
  SearchResult r;
  r.clauses = parse_query(query);
  analyze_clauses(r);
  return std::move(r.terms);
}

SearchResult Searcher::search(std::string_view query, std::size_t k) const {
  SearchResult result;
  result.clauses = parse_query(query);
  analyze_clauses(result);
  TopK top(k);
  if (result.phrases.empty()) {
    search_or(result, top);
  } else {
    search_with_phrases(result, top);
  }
  result.hits = std::move(top).take_sorted();
  return result;
}

// Free words only: disjunctive DAAT.
void Searcher::search_or(SearchResult& result, TopK& top) const {
  std::vector<PostingCursor> cursors;
  cursors.reserve(result.terms.size());
  for (const QueryTerm& qt : result.terms) {
    cursors.emplace_back(index_->posting_list(qt.term));
  }

  while (true) {
    // With a handful of query terms, a linear scan for the minimum beats a heap.
    DocId doc = kNoMoreDocs;
    for (const auto& c : cursors) {
      doc = std::min(doc, c.doc());
    }
    if (doc == kNoMoreDocs) {
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
}

// At least one phrase: conjunctive over phrase words, then a positional check.
void Searcher::search_with_phrases(SearchResult& result, TopK& top) const {
  // One cursor per phrase word occurrence, flattened. Phrase p owns the
  // cursors in [first[p], first[p + 1]).
  std::vector<PostingCursor> required;
  std::vector<std::size_t> first{0};
  for (const QueryPhrase& ph : result.phrases) {
    for (const auto& term : ph.terms) {
      required.emplace_back(index_->posting_list(term));
    }
    first.push_back(required.size());
  }
  std::vector<PostingCursor> optional;
  optional.reserve(result.terms.size());
  for (const QueryTerm& qt : result.terms) {
    optional.emplace_back(index_->posting_list(qt.term));
  }

  std::vector<std::span<const std::uint32_t>> positions;  // scratch, reused per phrase
  for (DocId doc = align(required); doc != kNoMoreDocs;
       required.front().next(), doc = align(required)) {
    // Every phrase word is present in `doc`; now check adjacency.
    double score = 0.0;
    const std::uint32_t doc_length = index_->doc_length(doc);
    bool all_phrases = true;
    for (std::size_t p = 0; p < result.phrases.size() && all_phrases; ++p) {
      positions.clear();
      for (std::size_t c = first[p]; c < first[p + 1]; ++c) {
        positions.push_back(required[c].positions());
      }
      const std::uint32_t occurrences = count_phrase_matches(positions, result.phrases[p].offsets);
      all_phrases = occurrences > 0;
      score += bm25_.score(result.phrases[p].idf, occurrences, doc_length);
    }
    if (!all_phrases) {
      continue;
    }
    for (std::size_t i = 0; i < optional.size(); ++i) {
      optional[i].advance_to(doc);  // candidate docs only increase, so this is safe
      if (optional[i].doc() == doc) {
        const auto weight = static_cast<double>(result.terms[i].query_tf);
        score += weight * bm25_.score(result.terms[i].idf, optional[i].tf(), doc_length);
      }
    }
    ++result.matched;
    top.push(ScoredDoc{doc, score});
  }
}

}  // namespace gaffer::query
