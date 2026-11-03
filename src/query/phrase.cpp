#include "query/phrase.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gaffer::query {

std::uint32_t count_phrase_matches(std::span<const std::span<const std::uint32_t>> positions,
                                   std::span<const std::uint32_t> offsets) noexcept {
  assert(!positions.empty() && positions.size() == offsets.size());
  std::uint32_t count = 0;
  for (const std::uint32_t start : positions[0]) {
    bool all = true;
    for (std::size_t j = 1; j < positions.size() && all; ++j) {
      all = std::ranges::binary_search(positions[j], start + offsets[j]);
    }
    if (all) {
      ++count;
    }
  }
  return count;
}

}  // namespace gaffer::query
