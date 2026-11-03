#include "index/inverted_index.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/error.hpp"
#include "common/types.hpp"
#include "corpus/corpus.hpp"
#include "text/analyzer.hpp"

namespace gaffer::index {

std::span<const std::uint32_t> PostingList::positions(std::size_t i) const noexcept {
  assert(i < postings_.size());
  return std::span(positions_).subspan(offsets_[i], offsets_[i + 1] - offsets_[i]);
}

void PostingList::append(DocId doc, std::span<const std::uint32_t> positions) {
  assert(!positions.empty());
  assert(postings_.empty() || postings_.back().doc < doc);
  assert(std::ranges::is_sorted(positions));
  positions_.insert(positions_.end(), positions.begin(), positions.end());
  postings_.push_back(Posting{doc, static_cast<std::uint32_t>(positions.size())});
  offsets_.push_back(static_cast<std::uint32_t>(positions_.size()));
}

void PostingList::shrink_to_fit() {
  postings_.shrink_to_fit();
  positions_.shrink_to_fit();
  offsets_.shrink_to_fit();
}

const PostingList& InvertedIndex::posting_list(std::string_view term) const {
  static const PostingList empty;        // returned for unknown terms; never modified
  const auto it = postings_.find(term);  // heterogeneous: no std::string built
  return it == postings_.end() ? empty : it->second;
}

std::span<const Posting> InvertedIndex::postings(std::string_view term) const {
  return posting_list(term).postings();
}

std::vector<std::string_view> InvertedIndex::sorted_terms() const {
  std::vector<std::string_view> terms;
  terms.reserve(postings_.size());
  for (const auto& [term, list] : postings_) {
    terms.emplace_back(term);
  }
  std::ranges::sort(terms);
  return terms;
}

std::uint32_t InvertedIndex::doc_freq(std::string_view term) const {
  // Each document appears at most once per postings list, so df = list length.
  return static_cast<std::uint32_t>(postings(term).size());
}

std::uint32_t InvertedIndex::doc_length(DocId doc) const noexcept {
  assert(doc < doc_lengths_.size());
  return doc_lengths_[doc];
}

IndexBuilder::IndexBuilder(text::Analyzer analyzer) noexcept : analyzer_(analyzer) {
  index_.analyzer_options_ = analyzer_.options();
}

DocId IndexBuilder::add(std::string_view text) {
  if (index_.doc_lengths_.size() >= std::numeric_limits<DocId>::max()) {
    throw Error("index is full: DocId space exhausted");
  }
  const auto doc = static_cast<DocId>(index_.doc_lengths_.size());
  const std::vector<text::Token> tokens = analyzer_.analyze(text);

  // Group this document's tokens by term. A stable sort by term keeps each
  // term's positions in ascending order (the tokenizer emits them ascending)
  // and visits terms in sorted order, so builds are deterministic. Terms are
  // views into `tokens`, which outlives `scratch_`'s use here.
  scratch_.clear();
  for (const auto& tok : tokens) {
    scratch_.push_back(TermPosition{tok.term, tok.position});
  }
  std::ranges::stable_sort(scratch_, {}, &TermPosition::term);

  // One posting per distinct term. Docs arrive in increasing id order, so every
  // postings list stays sorted without an explicit sort.
  for (std::size_t i = 0; i < scratch_.size();) {
    const std::string_view term = scratch_[i].term;
    run_.clear();
    for (; i < scratch_.size() && scratch_[i].term == term; ++i) {
      run_.push_back(scratch_[i].position);
    }
    auto it = index_.postings_.find(term);
    if (it == index_.postings_.end()) {
      it = index_.postings_.emplace(std::string(term), PostingList{}).first;
    }
    it->second.append(doc, run_);
  }
  scratch_.clear();  // drop views into `tokens` before it is destroyed

  const auto length = static_cast<std::uint32_t>(tokens.size());
  index_.doc_lengths_.push_back(length);
  index_.stats_.doc_count += 1;
  index_.stats_.total_length += length;
  return doc;
}

InvertedIndex IndexBuilder::build() && {
  std::size_t postings = 0;
  std::size_t positions = 0;
  for (auto& [term, list] : index_.postings_) {
    list.shrink_to_fit();  // builder over-allocates while growing; trim before freezing
    postings += list.size();
    positions += list.position_count();
  }
  index_.posting_count_ = postings;
  index_.position_count_ = positions;
  return std::move(index_);
}

std::string indexed_text(const corpus::Document& doc) {
  std::string text;
  text.reserve(doc.title.size() + 1 + doc.body.size());
  text.append(doc.title).append("\n").append(doc.body);
  return text;
}

InvertedIndex build_index(const corpus::Corpus& corpus, const text::Analyzer& analyzer) {
  IndexBuilder builder(analyzer);
  for (const auto& doc : corpus.documents()) {
    builder.add(indexed_text(doc));
  }
  return std::move(builder).build();
}

}  // namespace gaffer::index
