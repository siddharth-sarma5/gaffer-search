#include "query/top_k.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace gaffer::query {

namespace {

// std::push_heap/pop_heap keep the *greatest* element (by the comparator) at
// the front. With "a ranks before b" as the comparator, the "greatest" element
// is the one that ranks before nothing: the worst kept result. That gives the
// min-heap-by-rank we want.
constexpr auto kWorstOnTop = [](const ScoredDoc& a, const ScoredDoc& b) {
  return ranks_before(a, b);
};

}  // namespace

void TopK::push(const ScoredDoc& candidate) {
  if (k_ == 0) {
    return;
  }
  if (heap_.size() < k_) {
    heap_.push_back(candidate);
    std::ranges::push_heap(heap_, kWorstOnTop);
    return;
  }
  // Full: the candidate must beat the current worst kept result.
  if (ranks_before(candidate, heap_.front())) {
    std::ranges::pop_heap(heap_, kWorstOnTop);  // moves the worst to the back
    heap_.back() = candidate;
    std::ranges::push_heap(heap_, kWorstOnTop);
  }
}

std::vector<ScoredDoc> TopK::take_sorted() && {
  std::ranges::sort(heap_, ranks_before);
  return std::move(heap_);
}

}  // namespace gaffer::query
