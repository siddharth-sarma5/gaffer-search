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

// All occurrences of one term: a postings list plus the positions behind each posting.
//
// Layout (structure of arrays):
//   postings_  [(d0, tf0), (d1, tf1), ...]          sorted by DocId
//   positions_ [p.. of d0 | p.. of d1 | ...]        each run sorted ascending
//   offsets_   [0, tf0, tf0+tf1, ...]                n+1 entries; run i = [offsets_[i],
//   offsets_[i+1])
// tf equals the number of positions, so offsets_ is just a prefix sum of tf.
// It is stored so positions(i) is O(1) instead of O(i).
// Keeping positions out of the Posting struct means BM25-only scans never
// touch position data, and each array delta-encodes cleanly later (M2).
class PostingList {
 public:
  PostingList() = default;

  [[nodiscard]] std::size_t size() const noexcept { return postings_.size(); }
  [[nodiscard]] bool empty() const noexcept { return postings_.empty(); }
  [[nodiscard]] std::span<const Posting> postings() const noexcept { return postings_; }
  // Positions of the i-th posting, ascending. Precondition: i < size().
  [[nodiscard]] std::span<const std::uint32_t> positions(std::size_t i) const noexcept;
  [[nodiscard]] std::size_t position_count() const noexcept { return positions_.size(); }

  // Appends a posting for `doc` (must be greater than the last doc added).
  // tf = positions.size(); positions must be ascending and non-empty.
  void append(DocId doc, std::span<const std::uint32_t> positions);
  void shrink_to_fit();

 private:
  std::vector<Posting> postings_;
  std::vector<std::uint32_t> positions_;
  std::vector<std::uint32_t> offsets_{0};
};

// term -> postings list. Invariant: every postings list is sorted by strictly
// increasing DocId. The builder guarantees this by adding documents in id
// order. Phrase matching, DAAT evaluation and delta compression (M2) all rely
// on it.
class InvertedIndex {
 public:
  // Empty span if the term isn't in the index.
  [[nodiscard]] std::span<const Posting> postings(std::string_view term) const;
  // Full list with positions. An empty list if the term isn't in the index.
  [[nodiscard]] const PostingList& posting_list(std::string_view term) const;
  // Document frequency: how many documents contain the term.
  [[nodiscard]] std::uint32_t doc_freq(std::string_view term) const;
  // Precondition: doc < stats().doc_count.
  [[nodiscard]] std::uint32_t doc_length(DocId doc) const noexcept;
  [[nodiscard]] const CollectionStats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::size_t term_count() const noexcept { return postings_.size(); }
  [[nodiscard]] std::size_t posting_count() const noexcept { return posting_count_; }
  [[nodiscard]] std::size_t position_count() const noexcept { return position_count_; }
  // Every term in the index, sorted (deterministic order for serialisation and tests).
  [[nodiscard]] std::vector<std::string_view> sorted_terms() const;
  // The analysis the index was built with. Queries must use the same.
  [[nodiscard]] const text::AnalyzerOptions& analyzer_options() const noexcept {
    return analyzer_options_;
  }

 private:
  friend class IndexBuilder;

  std::unordered_map<std::string, PostingList, StringHash, std::equal_to<>> postings_;
  std::vector<std::uint32_t> doc_lengths_;
  CollectionStats stats_;
  std::size_t posting_count_ = 0;
  std::size_t position_count_ = 0;
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
  // Scratch buffers reused across add() calls to avoid reallocating per doc.
  struct TermPosition {
    std::string_view term;
    std::uint32_t position;
  };
  std::vector<TermPosition> scratch_;
  std::vector<std::uint32_t> run_;
};

// The text indexed for a corpus document: title, then body.
[[nodiscard]] std::string indexed_text(const corpus::Document& doc);

// Builds an index over every document in `corpus`; DocIds match corpus positions.
[[nodiscard]] InvertedIndex build_index(const corpus::Corpus& corpus,
                                        const text::Analyzer& analyzer);

}  // namespace gaffer::index
