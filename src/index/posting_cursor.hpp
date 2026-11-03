#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "common/types.hpp"
#include "index/inverted_index.hpp"

namespace gaffer::index {

// Sentinel doc id returned by an exhausted cursor. It compares greater than
// every real DocId, so "smallest current doc across cursors" needs no special
// case for finished cursors. Valid because a Corpus holds at most
// numeric_limits<DocId>::max() docs, so the largest real id is max() - 1.
inline constexpr DocId kNoMoreDocs = std::numeric_limits<DocId>::max();

// Forward-only iterator over one PostingList, the building block of
// document-at-a-time (DAAT) evaluation.
//
//   for (PostingCursor c(list); !c.at_end(); c.next()) { c.doc(); c.tf(); c.positions(); }
//
// Non-owning: the PostingList must outlive the cursor.
class PostingCursor {
 public:
  explicit PostingCursor(const PostingList& list) noexcept : list_(&list) {}

  [[nodiscard]] bool at_end() const noexcept { return i_ >= list_->size(); }
  [[nodiscard]] DocId doc() const noexcept {
    return at_end() ? kNoMoreDocs : list_->postings()[i_].doc;
  }
  // Preconditions for tf()/positions(): !at_end().
  [[nodiscard]] std::uint32_t tf() const noexcept { return list_->postings()[i_].tf; }
  [[nodiscard]] std::span<const std::uint32_t> positions() const noexcept {
    return list_->positions(i_);
  }
  [[nodiscard]] std::size_t remaining() const noexcept { return at_end() ? 0 : list_->size() - i_; }

  void next() noexcept { ++i_; }

  // Moves to the first posting with doc >= target. Never moves backwards: if
  // the cursor is already at or past target, nothing happens.
  //
  // Galloping (exponential) search: probe i+1, i+2, i+4, i+8, ... until we
  // overshoot, then binary-search the last interval. Costs O(log d) where d is
  // the distance moved, so short hops (the common case when intersecting
  // lists of similar length) are cheap, and long skips over a dense list
  // don't scan linearly.
  void advance_to(DocId target) noexcept;

 private:
  const PostingList* list_;
  std::size_t i_ = 0;
};

}  // namespace gaffer::index
