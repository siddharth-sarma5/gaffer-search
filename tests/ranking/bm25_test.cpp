#include "ranking/bm25.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "common/error.hpp"

namespace gaffer::ranking {
namespace {

// Reference values computed independently in Python (see the M1-15 commit message):
//   idf(N, df)  = ln(1 + (N - df + 0.5) / (df + 0.5))
//   score       = idf * tf * (k1 + 1) / (tf + k1 * (1 - b + b * dl / avgdl))
constexpr double kTol = 1e-12;

TEST(Bm25Test, IdfMatchesHandComputedValues) {
  const Bm25 small({}, 3, 1.0);
  EXPECT_NEAR(small.idf(1), 0.9808292530117263, kTol);
  EXPECT_NEAR(small.idf(3), 0.13353139262452257, kTol);  // in every doc: still positive
  const Bm25 big({}, 1000, 1.0);
  EXPECT_NEAR(big.idf(1), 6.503289671207057, kTol);
}

TEST(Bm25Test, IdfDecreasesAsTermsGetCommoner) {
  const Bm25 bm({}, 100, 10.0);
  for (std::uint32_t df = 1; df < 100; ++df) {
    EXPECT_GT(bm.idf(df), bm.idf(df + 1)) << df;
    EXPECT_GT(bm.idf(df + 1), 0.0);
  }
}

TEST(Bm25Test, ScoreMatchesHandComputedValues) {
  const Bm25 bm({}, 3, 11.0 / 3.0);  // the 3-doc collection from the index tests
  EXPECT_NEAR(bm.score(bm.idf(1), 2, 4), 1.3150176134561649, kTol);
  EXPECT_NEAR(bm.score(bm.idf(2), 1, 4), 0.4531509094719841, kTol);
}

TEST(Bm25Test, NonDefaultParameters) {
  // b = 0: length is ignored; with tf = 1, score = idf.
  const Bm25 no_len({.k1 = 1.5, .b = 0.0}, 10, 4.0);
  EXPECT_NEAR(no_len.score(no_len.idf(5), 1, 8), 0.6931471805599453, kTol);
  // b = 1: a doc twice the average length is fully penalised.
  const Bm25 full({.k1 = 1.2, .b = 1.0}, 10, 4.0);
  EXPECT_NEAR(full.score(full.idf(1), 1, 8), 1.289219518328957, kTol);
  // k1 = 0: tf is ignored entirely (binary model), score = idf.
  const Bm25 binary({.k1 = 0.0, .b = 0.75}, 10, 4.0);
  EXPECT_NEAR(binary.score(binary.idf(1), 5, 8), 1.9924301646902058, kTol);
}

TEST(Bm25Test, TermFrequencySaturates) {
  const Bm25 bm({}, 10, 4.0);
  const double idf = bm.idf(1);
  // Upper bound as tf -> infinity is idf * (k1 + 1).
  EXPECT_NEAR(bm.score(idf, 100, 4), 4.331369923239579, kTol);
  EXPECT_LT(bm.score(idf, 100, 4), idf * 2.2);
  // Each extra occurrence adds less than the one before.
  double prev_gain = std::numeric_limits<double>::infinity();
  for (std::uint32_t tf = 1; tf < 20; ++tf) {
    const double gain = bm.score(idf, tf + 1, 4) - bm.score(idf, tf, 4);
    EXPECT_GT(gain, 0.0);
    EXPECT_LT(gain, prev_gain);
    prev_gain = gain;
  }
}

TEST(Bm25Test, LongerDocumentsScoreLowerForTheSameTf) {
  const Bm25 bm({}, 10, 100.0);
  const double idf = bm.idf(2);
  EXPECT_GT(bm.score(idf, 1, 30), bm.score(idf, 1, 100));
  EXPECT_GT(bm.score(idf, 1, 100), bm.score(idf, 1, 300));
}

TEST(Bm25Test, EmptyCollectionDoesNotDivideByZero) {
  const Bm25 bm({}, 0, 0.0);
  EXPECT_TRUE(std::isfinite(bm.score(1.0, 1, 0)));
}

TEST(Bm25Test, RejectsInvalidParameters) {
  EXPECT_THROW(Bm25({.k1 = -0.1, .b = 0.75}, 1, 1.0), Error);
  EXPECT_THROW(Bm25({.k1 = 1.2, .b = 1.5}, 1, 1.0), Error);
  EXPECT_THROW(Bm25({.k1 = 1.2, .b = -0.5}, 1, 1.0), Error);
  EXPECT_THROW(Bm25({.k1 = std::numeric_limits<double>::quiet_NaN(), .b = 0.5}, 1, 1.0), Error);
}

}  // namespace
}  // namespace gaffer::ranking
