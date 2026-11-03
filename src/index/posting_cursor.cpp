#include "index/posting_cursor.hpp"

#include <algorithm>
#include <cstddef>

#include "common/types.hpp"

namespace gaffer::index {

void PostingCursor::advance_to(DocId target) noexcept {
  if (at_end() || doc() >= target) {
    return;
  }
  const auto postings = list_->postings();
  const std::size_t n = postings.size();

  // Gallop: find bounds lo < hi with postings[lo].doc < target <= postings[hi].doc
  // (hi may be n, meaning "past the end").
  std::size_t lo = i_;
  std::size_t step = 1;
  std::size_t hi = i_ + step;
  while (hi < n && postings[hi].doc < target) {
    lo = hi;
    step *= 2;
    hi = lo + step;
  }
  hi = std::min(hi, n);

  // Binary search in (lo, hi] for the first doc >= target.
  const auto first = postings.begin() + static_cast<std::ptrdiff_t>(lo + 1);
  const auto last = postings.begin() + static_cast<std::ptrdiff_t>(hi);
  const auto it =
      std::lower_bound(first, last, target, [](const Posting& p, DocId t) { return p.doc < t; });
  i_ = static_cast<std::size_t>(it - postings.begin());
}

}  // namespace gaffer::index
