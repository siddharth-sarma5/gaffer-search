#include "query/searcher.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "index/inverted_index.hpp"
#include "ranking/bm25.hpp"
#include "text/analyzer.hpp"

namespace gaffer::query {
namespace {

class SearcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    index::IndexBuilder b{text::Analyzer{}};
    b.add("Salah scores late winner at Anfield");            // 0
    b.add("Haaland scores twice at the Etihad");             // 1
    b.add("Salah Salah Salah");                              // 2
    b.add("Arsenal draw at Anfield with a late equaliser");  // 3
    idx_ = std::move(b).build();
  }
  index::InvertedIndex idx_;
};

TEST_F(SearcherTest, ScoresAreSumsOfPerTermBm25) {
  const Searcher s(idx_);
  const SearchResult r = s.search("late anfield", 10);
  ASSERT_EQ(r.hits.size(), 2U);

  const ranking::Bm25 bm({}, idx_.stats().doc_count, idx_.stats().avg_doc_length());
  for (const ScoredDoc& hit : r.hits) {
    double expected = 0;
    for (const auto* t : {"late", "anfield"}) {
      for (const auto& p : idx_.postings(t)) {
        if (p.doc == hit.doc) {
          expected += bm.score(bm.idf(idx_.doc_freq(t)), p.tf, idx_.doc_length(p.doc));
        }
      }
    }
    EXPECT_NEAR(hit.score, expected, 1e-12);
  }
  // Doc 0 is shorter than doc 3 (5 vs 6 terms) with the same tfs, so it ranks first.
  EXPECT_EQ(r.hits[0].doc, 0U);
  EXPECT_EQ(r.hits[1].doc, 3U);
  EXPECT_EQ(r.matched, 2U);
}

TEST_F(SearcherTest, QueryTermsAreAnalysedAndDeduplicated) {
  const Searcher s(idx_);
  const auto terms = s.query_terms("The SCORES, scores at Anfield!");
  ASSERT_EQ(terms.size(), 2U);
  EXPECT_EQ(terms[0].term, "score");
  EXPECT_EQ(terms[0].query_tf, 2U);
  EXPECT_EQ(terms[0].doc_freq, 2U);
  EXPECT_EQ(terms[1].term, "anfield");
  EXPECT_GT(terms[1].idf, 0.0);
}

TEST_F(SearcherTest, TermFrequencySaturationRanksFocusedDocFirst) {
  // Doc 2 says "salah" 3 times in a 3-term doc: highest tf and shortest.
  const SearchResult r = Searcher(idx_).search("salah", 10);
  ASSERT_EQ(r.hits.size(), 2U);
  EXPECT_EQ(r.hits[0].doc, 2U);
  EXPECT_EQ(r.hits[1].doc, 0U);
}

TEST_F(SearcherTest, RespectsKAndHandlesNoMatches) {
  const Searcher s(idx_);
  EXPECT_EQ(s.search("scores", 1).hits.size(), 1U);
  EXPECT_EQ(s.search("scores", 1).matched, 2U);
  EXPECT_TRUE(s.search("mbappe", 10).hits.empty());
  EXPECT_TRUE(s.search("", 10).hits.empty());
  EXPECT_TRUE(s.search("the at a", 10).hits.empty());  // only stopwords
}

TEST_F(SearcherTest, UsesTheIndexAnalyzerSettings) {
  index::IndexBuilder b{text::Analyzer{{.remove_stopwords = true, .stem = false}}};
  b.add("goals galore");
  const index::InvertedIndex unstemmed = std::move(b).build();
  // If the searcher stemmed "goals" -> "goal" it would miss this unstemmed index.
  EXPECT_EQ(Searcher(unstemmed).search("goals", 10).hits.size(), 1U);
}

// Property test: DAAT search equals exhaustive scoring of every document.
// Random documents over a tiny vocabulary give lots of overlap and ties.
TEST(SearcherPropertyTest, MatchesBruteForceScoring) {
  const std::vector<std::string> vocab{"salah",   "haaland", "goal",    "late", "winner",
                                       "anfield", "derby",   "penalty", "red",  "card"};
  std::mt19937 rng(1886);
  for (int trial = 0; trial < 50; ++trial) {
    index::IndexBuilder b{text::Analyzer{}};
    const int n_docs = 1 + static_cast<int>(rng() % 40);
    for (int d = 0; d < n_docs; ++d) {
      std::string text;
      const auto len = rng() % 12;
      for (std::uint32_t w = 0; w < len; ++w) {
        text += vocab[rng() % vocab.size()] + " ";
      }
      b.add(text);
    }
    const index::InvertedIndex idx = std::move(b).build();
    const Searcher s(idx);

    std::string q;
    for (std::uint32_t w = 0; w < 1 + rng() % 4; ++w) {
      q += vocab[rng() % vocab.size()] + " ";
    }
    const std::size_t k = 1 + rng() % 8;

    // Reference: score every doc by walking each term's full postings.
    const auto terms = s.query_terms(q);
    const ranking::Bm25 bm({}, idx.stats().doc_count, idx.stats().avg_doc_length());
    std::vector<double> score(idx.stats().doc_count, 0.0);
    std::vector<bool> hit(idx.stats().doc_count, false);
    for (const auto& qt : terms) {
      for (const auto& p : idx.postings(qt.term)) {
        score[p.doc] += qt.query_tf * bm.score(qt.idf, p.tf, idx.doc_length(p.doc));
        hit[p.doc] = true;
      }
    }
    std::vector<ScoredDoc> all;
    for (DocId d = 0; d < score.size(); ++d) {
      if (hit[d]) {
        all.push_back({d, score[d]});
      }
    }
    std::ranges::sort(all, ranks_before);
    const std::size_t matched = all.size();
    all.resize(std::min(k, all.size()));

    const SearchResult r = s.search(q, k);
    EXPECT_EQ(r.matched, matched) << "trial " << trial;
    ASSERT_EQ(r.hits.size(), all.size()) << "trial " << trial;
    for (std::size_t i = 0; i < all.size(); ++i) {
      EXPECT_EQ(r.hits[i].doc, all[i].doc) << "trial " << trial << " rank " << i;
      EXPECT_DOUBLE_EQ(r.hits[i].score, all[i].score) << "trial " << trial << " rank " << i;
    }
  }
}

}  // namespace
}  // namespace gaffer::query
