#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "corpus/corpus.hpp"
#include "text/analyzer.hpp"

namespace gaffer::index {

// One entry in a postings list: "term t occurs tf times in document doc".
struct Posting {
  DocId doc;
  std::uint32_t tf;

  friend bool operator==(const Posting&, const Posting&) = default;
};

// Collection-wide numbers BM25 needs: N and the average document length.
struct CollectionStats {
  std::uint32_t doc_count = 0;
  std::uint64_t total_length = 0;  // sum of all document lengths, in analysed tokens

  [[nodiscard]] double avg_doc_length() const noexcept {
    return doc_count == 0 ? 0.0
                          : static_cast<double>(total_length) / static_cast<double>(doc_count);
  }
};

// Hash that accepts std::string and std::string_view alike, so lookups with a
// string_view don't allocate a temporary std::string. This is C++20
// "heterogeneous lookup"; it needs both is_transparent and std::equal_to<>.
struct StringHash {
  using is_transparent = void;
  [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
};

// term -> postings list. Invariant: every postings list is sorted by strictly
// increasing DocId. The builder guarantees this by adding documents in id
// order. Phrase matching, DAAT evaluation and delta compression (M2) all rely
// on it.
class InvertedIndex {
 public:
  // Empty span if the term isn't in the index.
  [[nodiscard]] std::span<const Posting> postings(std::string_view term) const;
  // Document frequency: how many documents contain the term.
  [[nodiscard]] std::uint32_t doc_freq(std::string_view term) const;
  // Precondition: doc < stats().doc_count.
  [[nodiscard]] std::uint32_t doc_length(DocId doc) const noexcept;
  [[nodiscard]] const CollectionStats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::size_t term_count() const noexcept { return postings_.size(); }
  [[nodiscard]] std::size_t posting_count() const noexcept { return posting_count_; }
  // The analysis the index was built with. Queries must use the same.
  [[nodiscard]] const text::AnalyzerOptions& analyzer_options() const noexcept {
    return analyzer_options_;
  }

 private:
  friend class IndexBuilder;

  std::unordered_map<std::string, std::vector<Posting>, StringHash, std::equal_to<>> postings_;
  std::vector<std::uint32_t> doc_lengths_;
  CollectionStats stats_;
  std::size_t posting_count_ = 0;
  text::AnalyzerOptions analyzer_options_;
};

// Accumulates documents one at a time, then hands over an immutable index.
//
//   IndexBuilder b(analyzer);
//   b.add("Liverpool beat Bournemouth 4-2");   // -> DocId 0
//   InvertedIndex idx = std::move(b).build();
class IndexBuilder {
 public:
  explicit IndexBuilder(text::Analyzer analyzer) noexcept;

  // Analyses `text` and adds it as the next document. Returns its DocId.
  DocId add(std::string_view text);

  // `&&`: building consumes the builder (call as std::move(b).build()), so the
  // postings are moved into the index instead of copied.
  [[nodiscard]] InvertedIndex build() &&;

 private:
  text::Analyzer analyzer_;
  InvertedIndex index_;
  // Scratch buffer reused across add() calls to avoid reallocating per doc.
  std::unordered_map<std::string_view, std::uint32_t> doc_tf_;
};

// The text indexed for a corpus document: title, then body.
[[nodiscard]] std::string indexed_text(const corpus::Document& doc);

// Builds an index over every document in `corpus`; DocIds match corpus positions.
[[nodiscard]] InvertedIndex build_index(const corpus::Corpus& corpus,
                                        const text::Analyzer& analyzer);

}  // namespace gaffer::index
