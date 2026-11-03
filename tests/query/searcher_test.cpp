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

std::vector<DocId> hit_docs(const SearchResult& r) {
  std::vector<DocId> out;
  for (const auto& h : r.hits) {
    out.push_back(h.doc);
  }
  std::ranges::sort(out);
  return out;
}

class PhraseSearchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    index::IndexBuilder b{text::Analyzer{}};
    b.add("Salah scored a late winner at Anfield");       // 0
    b.add("A winner arrived late at the Etihad");         // 1: both words, not adjacent
    b.add("Late winner, late winner! Stadium of Light");  // 2: phrase twice
    b.add("Stadium and Light show");                      // 3: "and" is a stopword too
    b.add("Haaland late winner");                         // 4
    idx_ = std::move(b).build();
  }
  index::InvertedIndex idx_;
};

TEST_F(PhraseSearchTest, PhraseRequiresAdjacency) {
  const SearchResult r = Searcher(idx_).search("\"late winner\"", 10);
  EXPECT_EQ(hit_docs(r), (std::vector<DocId>{0, 2, 4}));
  EXPECT_EQ(r.matched, 3U);
  ASSERT_EQ(r.phrases.size(), 1U);
  EXPECT_EQ(r.phrases[0].offsets, (std::vector<std::uint32_t>{0, 1}));
  // The unquoted query also matches doc 1.
  EXPECT_EQ(hit_docs(Searcher(idx_).search("late winner", 10)), (std::vector<DocId>{0, 1, 2, 4}));
}

TEST_F(PhraseSearchTest, StopwordGapsMustMatchButAnyStopwordFillsThem) {
  // offsets {0, 2}: doc 2 ("of") and doc 3 ("and") both have a one-word gap.
  EXPECT_EQ(hit_docs(Searcher(idx_).search("\"stadium of light\"", 10)),
            (std::vector<DocId>{2, 3}));
  EXPECT_TRUE(Searcher(idx_).search("\"stadium light\"", 10).hits.empty());
}

TEST_F(PhraseSearchTest, PhraseScoreIsBm25OfOccurrencesWithSummedIdf) {
  const Searcher s(idx_);
  const SearchResult r = s.search("\"late winner\"", 10);
  const ranking::Bm25 bm({}, idx_.stats().doc_count, idx_.stats().avg_doc_length());
  const double idf = bm.idf(idx_.doc_freq("late")) + bm.idf(idx_.doc_freq("winner"));
  EXPECT_NEAR(r.phrases[0].idf, idf, 1e-12);
  for (const auto& h : r.hits) {
    const std::uint32_t occurrences = h.doc == 2 ? 2 : 1;
    EXPECT_NEAR(h.score, bm.score(idf, occurrences, idx_.doc_length(h.doc)), 1e-12) << h.doc;
  }
}

TEST_F(PhraseSearchTest, FreeWordsOnlyBoostDocsThatMatchThePhrase) {
  const SearchResult r = Searcher(idx_).search("haaland \"late winner\"", 10);
  EXPECT_EQ(hit_docs(r), (std::vector<DocId>{0, 2, 4}));
  EXPECT_EQ(r.hits.front().doc, 4U);  // the only one that also has "haaland"
}

TEST_F(PhraseSearchTest, MultiplePhrasesAreAllRequired) {
  EXPECT_EQ(hit_docs(Searcher(idx_).search("\"late winner\" \"stadium of light\"", 10)),
            (std::vector<DocId>{2}));
}

TEST_F(PhraseSearchTest, UnknownWordOrStopwordOnlyPhrase) {
  EXPECT_TRUE(Searcher(idx_).search("\"late mbappe\"", 10).hits.empty());
  // A phrase of only stopwords imposes nothing; with nothing else, nothing matches.
  const SearchResult r = Searcher(idx_).search("\"the\"", 10);
  EXPECT_TRUE(r.phrases.empty());
  EXPECT_TRUE(r.hits.empty());
  // ...but alongside free words it's simply ignored.
  EXPECT_EQ(hit_docs(Searcher(idx_).search("\"the\" haaland", 10)), (std::vector<DocId>{4}));
}

class ExpansionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    index::IndexBuilder b{text::Analyzer{}};
    b.add("Arsenal won the match");   // 0
    b.add("The Gunners celebrated");  // 1
    b.add("Chelsea won the match");   // 2
    idx_ = std::move(b).build();
  }
  index::InvertedIndex idx_;
  NicknameExpander expander_{{{"gunners", "Arsenal"}}};
};

TEST_F(ExpansionTest, AliasAddsDownWeightedExpansionTerm) {
  const SearchResult r = Searcher(idx_, {}, &expander_).search("gunners", 10);
  ASSERT_EQ(r.terms.size(), 2U);
  EXPECT_EQ(r.terms[0].term, "gunner");
  EXPECT_DOUBLE_EQ(r.terms[0].weight, 1.0);
  EXPECT_EQ(r.terms[1].term, "arsenal");
  EXPECT_DOUBLE_EQ(r.terms[1].weight, kExpansionWeight);
  EXPECT_EQ(r.terms[1].expanded_from, "gunners");
  EXPECT_EQ(hit_docs(r), (std::vector<DocId>{0, 1}));

  // The expansion's contribution is exactly kExpansionWeight times plain BM25.
  const ranking::Bm25 bm({}, idx_.stats().doc_count, idx_.stats().avg_doc_length());
  for (const auto& h : r.hits) {
    if (h.doc == 0) {
      EXPECT_NEAR(h.score, kExpansionWeight * bm.score(bm.idf(1), 1, idx_.doc_length(0)), 1e-12);
    }
  }
}

TEST_F(ExpansionTest, WithoutExpanderNothingIsAdded) {
  EXPECT_EQ(hit_docs(Searcher(idx_).search("gunners", 10)), (std::vector<DocId>{1}));
}

TEST_F(ExpansionTest, TypedTermKeepsFullWeight) {
  // "arsenal" is typed AND produced by the alias: it must stay at weight 1.
  const SearchResult r = Searcher(idx_, {}, &expander_).search("gunners arsenal", 10);
  const auto it = std::ranges::find(r.terms, std::string("arsenal"), &QueryTerm::term);
  ASSERT_NE(it, r.terms.end());
  EXPECT_DOUBLE_EQ(it->weight, 1.0);
  EXPECT_TRUE(it->expanded_from.empty());
  EXPECT_EQ(r.terms.size(), 2U);  // no duplicate
}

TEST_F(ExpansionTest, PhrasesAreNotExpanded) {
  const SearchResult r = Searcher(idx_, {}, &expander_).search("\"gunners celebrated\"", 10);
  EXPECT_TRUE(r.terms.empty());
  EXPECT_EQ(hit_docs(r), (std::vector<DocId>{1}));
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

// Property test: phrase search returns exactly the docs where the two words are
// adjacent, checked against a naive re-tokenisation of every document.
TEST(SearcherPropertyTest, PhraseMatchesNaiveAdjacencyScan) {
  const std::vector<std::string> vocab{"late", "winner", "salah", "goal", "red", "card"};
  std::mt19937 rng(2025);
  for (int trial = 0; trial < 50; ++trial) {
    index::IndexBuilder b{text::Analyzer{}};
    std::vector<std::vector<std::string>> docs;
    const int n_docs = 1 + static_cast<int>(rng() % 40);
    for (int d = 0; d < n_docs; ++d) {
      std::vector<std::string> words;
      std::string text;
      for (std::uint32_t w = 0; w < rng() % 15; ++w) {
        words.push_back(vocab[rng() % vocab.size()]);
        text += words.back() + " ";
      }
      docs.push_back(words);
      b.add(text);
    }
    const index::InvertedIndex idx = std::move(b).build();
    const std::string w1 = vocab[rng() % vocab.size()];
    const std::string w2 = vocab[rng() % vocab.size()];

    std::vector<DocId> expected;
    for (DocId d = 0; d < docs.size(); ++d) {
      for (std::size_t i = 0; i + 1 < docs[d].size(); ++i) {
        if (docs[d][i] == w1 && docs[d][i + 1] == w2) {
          expected.push_back(d);
          break;
        }
      }
    }
    const SearchResult r = Searcher(idx).search("\"" + w1 + " " + w2 + "\"", 1000);
    EXPECT_EQ(hit_docs(r), expected) << "trial " << trial << " phrase " << w1 << " " << w2;
  }
}

}  // namespace
}  // namespace gaffer::query
