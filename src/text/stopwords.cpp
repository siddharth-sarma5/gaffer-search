#include "text/stopwords.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace gaffer::text {

namespace {

using namespace std::string_view_literals;

// Must stay sorted: lookup is a binary search (checked by a static_assert below).
constexpr std::array kStopwords{
    "a"sv,   "an"sv,   "and"sv,   "are"sv,   "as"sv,   "at"sv,   "be"sv,   "but"sv,
    "by"sv,  "for"sv,  "from"sv,  "had"sv,   "has"sv,  "he"sv,   "her"sv,  "his"sv,
    "in"sv,  "into"sv, "is"sv,    "it"sv,    "its"sv,  "of"sv,   "on"sv,   "or"sv,
    "she"sv, "that"sv, "the"sv,   "their"sv, "them"sv, "they"sv, "this"sv, "to"sv,
    "was"sv, "were"sv, "which"sv, "who"sv,   "will"sv, "with"sv,
};

static_assert(std::ranges::is_sorted(kStopwords), "kStopwords must be sorted");

}  // namespace

bool is_stopword(std::string_view term) noexcept {
  return std::ranges::binary_search(kStopwords, term);
}

}  // namespace gaffer::text
