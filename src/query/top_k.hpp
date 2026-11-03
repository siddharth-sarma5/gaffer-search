#pragma once

#include <cstddef>
#include <vector>

#include "common/types.hpp"

namespace gaffer::query {

struct ScoredDoc {
  DocId doc;
  double score;
};

// Total order used for ranking everywhere: higher score first, and on equal
// score the lower DocId first. The tie-break makes results deterministic, and
// in M3 it lets merged shard results be compared exactly with single-node results.
[[nodiscard]] constexpr bool ranks_before(const ScoredDoc& a, const ScoredDoc& b) noexcept {
  if (a.score != b.score) {
    return a.score > b.score;
  }
  return a.doc < b.doc;
}

// Keeps the k best of a stream of scored documents.
//
// Implementation: a binary heap of at most k entries whose root is the WORST
// entry kept (a "min-heap" by rank). A new candidate only has to beat the
// root; if it does, the root is evicted. So:
//   push:  O(log k)    total for n candidates: O(n log k) time, O(k) memory
// versus sorting all n candidates: O(n log n) time, O(n) memory. With
// n = 900 matches and k = 10 that's ~3.3 vs ~9.8 comparisons per candidate.
class TopK {
 public:
  explicit TopK(std::size_t k) : k_(k) { heap_.reserve(k); }

  void push(const ScoredDoc& candidate);

  [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }

  // Results best-first. Consumes the TopK.
  [[nodiscard]] std::vector<ScoredDoc> take_sorted() &&;

 private:
  std::size_t k_;
  std::vector<ScoredDoc> heap_;
};

}  // namespace gaffer::query
