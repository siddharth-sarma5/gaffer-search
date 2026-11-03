#include "query/searcher.hpp"

#include <utility>

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

}  // namespace
}  // namespace gaffer::query
