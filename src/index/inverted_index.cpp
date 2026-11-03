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

std::span<const Posting> InvertedIndex::postings(std::string_view term) const {
  const auto it = postings_.find(term);  // heterogeneous: no std::string built
  if (it == postings_.end()) {
    return {};
  }
  return it->second;
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

  // Count term frequencies within this document. Keys are views into `tokens`,
  // which outlives the map's use below, so no strings are copied here.
  doc_tf_.clear();
  for (const auto& tok : tokens) {
    ++doc_tf_[tok.term];
  }

  // Append one posting per distinct term. Docs arrive in increasing id order,
  // so every postings list stays sorted without an explicit sort.
  for (const auto& [term, tf] : doc_tf_) {
    auto it = index_.postings_.find(term);
    if (it == index_.postings_.end()) {
      it = index_.postings_.emplace(std::string(term), std::vector<Posting>{}).first;
    }
    it->second.push_back(Posting{doc, tf});
  }
  doc_tf_.clear();  // drop views into `tokens` before it is destroyed

  const auto length = static_cast<std::uint32_t>(tokens.size());
  index_.doc_lengths_.push_back(length);
  index_.stats_.doc_count += 1;
  index_.stats_.total_length += length;
  return doc;
}

InvertedIndex IndexBuilder::build() && {
  std::size_t total = 0;
  for (auto& [term, list] : index_.postings_) {
    list.shrink_to_fit();  // builder over-allocates while growing; trim before freezing
    total += list.size();
    assert(std::ranges::is_sorted(list, {}, &Posting::doc));
  }
  index_.posting_count_ = total;
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
