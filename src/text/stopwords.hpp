#pragma once

#include <string_view>

namespace gaffer::text {

// True for very common English function words ("the", "at", "of", ...).
//
// Why remove them: they appear in nearly every document, so their IDF is close
// to zero and they add little to ranking, yet their postings lists are the
// longest in the index. Dropping them shrinks the index and speeds up queries.
//
// The list is deliberately short and avoids words that carry football meaning:
// "off" (sent off), "home"/"away" (home win), "down" (came from two goals
// down) and "no" stay searchable.
//
// Expects an already lower-cased term.
[[nodiscard]] bool is_stopword(std::string_view term) noexcept;

}  // namespace gaffer::text
