#include "common/error.hpp"

#include <gtest/gtest.h>

namespace gaffer {
namespace {

TEST(ParseErrorTest, MessageIncludesSourceAndLine) {
  const ParseError err("corpus.tsv", 42, "expected 4 fields, got 3");
  EXPECT_STREQ(err.what(), "corpus.tsv:42: expected 4 fields, got 3");
  EXPECT_EQ(err.source(), "corpus.tsv");
  EXPECT_EQ(err.line(), 42U);
}

TEST(ParseErrorTest, IsCatchableAsGafferError) {
  EXPECT_THROW(throw ParseError("x", 1, "bad"), Error);
}

TEST(IoErrorTest, IsCatchableAsGafferError) {
  EXPECT_THROW(throw IoError("cannot open"), Error);
}

}  // namespace
}  // namespace gaffer
