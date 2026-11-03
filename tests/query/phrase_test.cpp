#include "query/phrase.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace gaffer::query {
namespace {

using List = std::vector<std::uint32_t>;

std::uint32_t count(const std::vector<List>& lists, const List& offsets) {
  std::vector<std::span<const std::uint32_t>> spans(lists.begin(), lists.end());
  return count_phrase_matches(spans, offsets);
}

TEST(CountPhraseMatchesTest, AdjacentWords) {
  // "late winner": late at {3, 10}, winner at {4, 20} -> one occurrence (3, 4).
  EXPECT_EQ(count({{3, 10}, {4, 20}}, {0, 1}), 1U);
}

TEST(CountPhraseMatchesTest, WrongOrderOrDistanceDoesNotMatch) {
  EXPECT_EQ(count({{5}, {4}}, {0, 1}), 0U);  // reversed
  EXPECT_EQ(count({{3}, {5}}, {0, 1}), 0U);  // a word in between
}

TEST(CountPhraseMatchesTest, StopwordGapOffsets) {
  // "stadium of light" -> offsets {0, 2}: matches a gap of exactly 2.
  EXPECT_EQ(count({{14, 90}, {16}}, {0, 2}), 1U);
  EXPECT_EQ(count({{14}, {15}}, {0, 2}), 0U);
}

TEST(CountPhraseMatchesTest, CountsEveryOccurrence) {
  EXPECT_EQ(count({{1, 7, 30}, {2, 8, 31}, {3, 9, 50}}, {0, 1, 2}), 2U);
}

TEST(CountPhraseMatchesTest, SingleWordPhraseCountsItsPositions) {
  EXPECT_EQ(count({{2, 5, 9}}, {0}), 3U);
}

TEST(CountPhraseMatchesTest, RepeatedWordInPhrase) {
  // "goal goal": the same positions list twice; positions 4, 5, 6 give 2 overlapping matches.
  EXPECT_EQ(count({{4, 5, 6}, {4, 5, 6}}, {0, 1}), 2U);
}

}  // namespace
}  // namespace gaffer::query
