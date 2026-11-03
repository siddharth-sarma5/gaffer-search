#include "index/postings_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "common/types.hpp"
#include "index/inverted_index.hpp"
#include "index/varint.hpp"

namespace gaffer::index {

std::vector<std::uint8_t> encode_posting_list(const PostingList& list) {
  std::vector<std::uint8_t> out;
  put_varint(out, list.size());
  DocId prev_doc = 0;
  for (std::size_t i = 0; i < list.size(); ++i) {
    const Posting& p = list.postings()[i];
    put_varint(out, i == 0 ? p.doc : p.doc - prev_doc);
    put_varint(out, p.tf);
    std::uint32_t prev_pos = 0;
    bool first = true;
    for (const std::uint32_t pos : list.positions(i)) {
      put_varint(out, first ? pos : pos - prev_pos);
      prev_pos = pos;
      first = false;
    }
    prev_doc = p.doc;
  }
  return out;
}

std::optional<PostingList> decode_posting_list(std::span<const std::uint8_t> bytes,
                                               DocId doc_count) {
  VarintReader in(bytes);
  const auto n = in.read_u32();
  // Each posting takes at least 3 bytes (doc gap, tf, one position). Checking
  // this up front stops a corrupt count from triggering a huge allocation.
  if (!n || *n > bytes.size() / 3 || *n > doc_count) {
    return std::nullopt;
  }

  PostingList list;
  std::vector<std::uint32_t> positions;
  std::uint64_t doc = 0;  // 64-bit so a corrupt gap can't wrap around unnoticed
  for (std::uint32_t i = 0; i < *n; ++i) {
    const auto gap = in.read_u32();
    if (!gap || (i > 0 && *gap == 0)) {
      return std::nullopt;
    }
    doc = (i == 0) ? *gap : doc + *gap;
    if (doc >= doc_count) {
      return std::nullopt;
    }
    const auto tf = in.read_u32();
    if (!tf || *tf == 0 || *tf > bytes.size()) {
      return std::nullopt;
    }
    positions.clear();
    std::uint64_t pos = 0;
    for (std::uint32_t j = 0; j < *tf; ++j) {
      const auto pos_gap = in.read_u32();
      if (!pos_gap || (j > 0 && *pos_gap == 0)) {
        return std::nullopt;
      }
      pos = (j == 0) ? *pos_gap : pos + *pos_gap;
      if (pos > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
      }
      positions.push_back(static_cast<std::uint32_t>(pos));
    }
    list.append(static_cast<DocId>(doc), positions);
  }
  if (!in.at_end()) {
    return std::nullopt;  // trailing garbage
  }
  list.shrink_to_fit();
  return list;
}

std::size_t fixed_width_size(const PostingList& list) noexcept {
  constexpr std::size_t kWord = sizeof(std::uint32_t);
  return list.size() * 3 * kWord + list.position_count() * kWord;
}

}  // namespace gaffer::index
