#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "common/types.hpp"
#include "index/inverted_index.hpp"

namespace gaffer::index {

// Compressed byte encoding of one PostingList: delta (d-gap) + varint.
//
//   varint n                         number of postings
//   n times:
//     varint doc_gap                 first posting: the doc id; then doc - previous doc (>= 1)
//     varint tf                      >= 1
//     tf times: varint pos_gap       first: the position; then position - previous (>= 1)
//
// Why deltas: postings lists and position lists are strictly increasing, so
// the gaps are much smaller than the values themselves. "anfield" in docs
// 3, 41, 52, ... becomes 3, 38, 11, ..., and most gaps fit in one varint byte.
// Frequent terms have the densest lists and so the smallest gaps; they
// compress best, and they are also the longest lists.
//
// Postings are decoded in full when an index file is loaded (D-017), so this
// layout favours size and simplicity over random access.
[[nodiscard]] std::vector<std::uint8_t> encode_posting_list(const PostingList& list);

// Decodes and validates bytes produced by encode_posting_list. Returns nullopt
// if the bytes are malformed in any way: bad varints, a zero gap (not strictly
// increasing), a doc id >= doc_count, tf = 0, position overflow, an implausible
// count, or trailing bytes. Never reads out of bounds.
[[nodiscard]] std::optional<PostingList> decode_posting_list(std::span<const std::uint8_t> bytes,
                                                             DocId doc_count);

// Size of the same list stored uncompressed as fixed-width uint32s: doc, tf and
// a position-offset per posting, plus one uint32 per position. This mirrors
// the in-memory PostingList layout and is the baseline compression is measured
// against.
[[nodiscard]] std::size_t fixed_width_size(const PostingList& list) noexcept;

}  // namespace gaffer::index
