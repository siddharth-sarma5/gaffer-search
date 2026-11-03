#include "index/posting_cursor.hpp"

#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "index/inverted_index.hpp"

namespace gaffer::index {
namespace {

PostingList make_list(const std::vector<DocId>& docs) {
  PostingList list;
  for (const DocId d : docs) {
    const std::vector<std::uint32_t> pos{d % 7, (d % 7) + 3};  // arbitrary, ascending
    list.append(d, pos);
  }
  return list;
}

TEST(PostingCursorTest, IteratesInOrderThenReportsSentinel) {
  const PostingList list = make_list({2, 5, 9});
  PostingCursor c(list);
  std::vector<DocId> seen;
  for (; !c.at_end(); c.next()) {
    seen.push_back(c.doc());
    EXPECT_EQ(c.tf(), 2U);
    EXPECT_EQ(c.positions().size(), 2U);
  }
  EXPECT_EQ(seen, (std::vector<DocId>{2, 5, 9}));
  EXPECT_EQ(c.doc(), kNoMoreDocs);
  EXPECT_EQ(c.remaining(), 0U);
}

TEST(PostingCursorTest, EmptyListStartsAtEnd) {
  const PostingList list;
  const PostingCursor c(list);
  EXPECT_TRUE(c.at_end());
  EXPECT_EQ(c.doc(), kNoMoreDocs);
}

TEST(PostingCursorTest, AdvanceToExactBetweenAndPastEnd) {
  const PostingList list = make_list({1, 4, 8, 15, 16, 23, 42});
  PostingCursor c(list);
  c.advance_to(8);
  EXPECT_EQ(c.doc(), 8U);  // exact hit
  c.advance_to(10);
  EXPECT_EQ(c.doc(), 15U);  // between: next doc >= target
  c.advance_to(3);
  EXPECT_EQ(c.doc(), 15U);  // never moves backwards
  c.advance_to(15);
  EXPECT_EQ(c.doc(), 15U);  // already there
  c.advance_to(43);
  EXPECT_TRUE(c.at_end());  // past the last doc
  c.advance_to(50);
  EXPECT_TRUE(c.at_end());  // advancing an exhausted cursor is a no-op
}

// Property test: galloping advance_to agrees with a plain linear scan.
TEST(PostingCursorTest, AdvanceToMatchesLinearScan) {
  std::mt19937 rng(868);
  for (int trial = 0; trial < 300; ++trial) {
    std::vector<DocId> docs;
    DocId d = 0;
    const auto n = rng() % 200;
    for (std::uint32_t i = 0; i < n; ++i) {
      d += 1 + static_cast<DocId>(rng() % 20);
      docs.push_back(d);
    }
    const PostingList list = make_list(docs);
    PostingCursor fast(list);
    std::size_t slow = 0;  // index into docs
    for (int step = 0; step < 30; ++step) {
      const DocId target = static_cast<DocId>(rng() % (d + 30));
      fast.advance_to(target);
      // Reference: from the current index, move forward until doc >= target.
      while (slow < docs.size() && docs[slow] < target) {
        ++slow;
      }
      const DocId want = slow < docs.size() ? docs[slow] : kNoMoreDocs;
      ASSERT_EQ(fast.doc(), want) << "trial " << trial << " target " << target;
    }
  }
}

}  // namespace
}  // namespace gaffer::index
