#pragma once

#include <cstdint>
#include <span>

namespace gaffer::query {

// Counts the occurrences of a phrase in one document.
//
// positions[j] is the ascending position list of the phrase's j-th word in the
// document; offsets[j] is where that word sits relative to the first word in
// the analysed query (offsets[0] == 0). The phrase occurs at p when every word
// j appears at p + offsets[j].
//
// Example: query "stadium of light" analyses to stadium@0, light@2 ("of" is a
// stopword but leaves its gap), so offsets = {0, 2}. A document with
// stadium at {14, 90} and light at {16} has one occurrence, at 14.
//
// Cost: O(|positions[0]| * m * log L) binary searches. Phrases are short, and
// a word's positions within one document are few.
//
// Preconditions: positions.size() == offsets.size() >= 1; each list ascending.
[[nodiscard]] std::uint32_t count_phrase_matches(
    std::span<const std::span<const std::uint32_t>> positions,
    std::span<const std::uint32_t> offsets) noexcept;

}  // namespace gaffer::query
