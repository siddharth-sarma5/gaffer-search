#include "index/inverted_index.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "corpus/corpus.hpp"
#include "corpus/document.hpp"
#include "text/analyzer.hpp"

namespace gaffer::index {
namespace {

using Postings = std::vector<Posting>;

Postings to_vec(std::span<const Posting> s) { return {s.begin(), s.end()}; }

class SmallIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    IndexBuilder b{text::Analyzer{}};
    b.add("Salah scores, Salah celebrates");      // 0: salah x2, score, celebrate
    b.add("Haaland scores twice at the Etihad");  // 1: at/the are stopwords
    b.add("Goals goals GOALS");                   // 2: goal x3 after stemming
    idx_ = std::move(b).build();
  }
  InvertedIndex idx_;
};

TEST_F(SmallIndexTest, PostingsHoldDocAndTermFrequency) {
  EXPECT_EQ(to_vec(idx_.postings("salah")), (Postings{{0, 2}}));
  EXPECT_EQ(to_vec(idx_.postings("score")), (Postings{{0, 1}, {1, 1}}));
  EXPECT_EQ(to_vec(idx_.postings("goal")), (Postings{{2, 3}}));
}

TEST_F(SmallIndexTest, LookupsUseAnalysedForms) {
  // The index stores stemmed, stopword-free terms. Raw forms aren't there.
  EXPECT_TRUE(idx_.postings("goals").empty());
  EXPECT_TRUE(idx_.postings("the").empty());
  EXPECT_TRUE(idx_.postings("unknown").empty());
  EXPECT_EQ(idx_.doc_freq("unknown"), 0U);
}

TEST_F(SmallIndexTest, DocumentFrequencyAndLengths) {
  EXPECT_EQ(idx_.doc_freq("score"), 2U);
  EXPECT_EQ(idx_.doc_length(0), 4U);  // salah score salah celebrate
  EXPECT_EQ(idx_.doc_length(1), 4U);  // haaland score twice etihad
  EXPECT_EQ(idx_.doc_length(2), 3U);
}

TEST_F(SmallIndexTest, CollectionStats) {
  EXPECT_EQ(idx_.stats().doc_count, 3U);
  EXPECT_EQ(idx_.stats().total_length, 11U);
  EXPECT_DOUBLE_EQ(idx_.stats().avg_doc_length(), 11.0 / 3.0);
  // Terms: salah score celebrate haaland twice etihad goal
  EXPECT_EQ(idx_.term_count(), 7U);
  EXPECT_EQ(idx_.posting_count(), 8U);
}

TEST(IndexBuilderTest, EmptyDocumentGetsAnIdAndZeroLength) {
  IndexBuilder b{text::Analyzer{}};
  EXPECT_EQ(b.add(""), DocId{0});
  EXPECT_EQ(b.add("the of at"), DocId{1});  // only stopwords
  const InvertedIndex idx = std::move(b).build();
  EXPECT_EQ(idx.doc_length(0), 0U);
  EXPECT_EQ(idx.doc_length(1), 0U);
  EXPECT_EQ(idx.term_count(), 0U);
  EXPECT_DOUBLE_EQ(idx.stats().avg_doc_length(), 0.0);
}

TEST(IndexBuilderTest, EmptyIndexHasZeroAverage) {
  EXPECT_DOUBLE_EQ(CollectionStats{}.avg_doc_length(), 0.0);
}

TEST(IndexBuilderTest, RecordsAnalyzerOptions) {
  const InvertedIndex idx =
      IndexBuilder{text::Analyzer{text::AnalyzerOptions{.remove_stopwords = true, .stem = false}}}
          .build();
  EXPECT_FALSE(idx.analyzer_options().stem);
  EXPECT_TRUE(idx.analyzer_options().remove_stopwords);
}

TEST(BuildIndexTest, IndexesTitleAndBody) {
  const corpus::Corpus c({{"a", corpus::DocType::kPlayer, "Erling Haaland", "Manchester City."}});
  const InvertedIndex idx = build_index(c, text::Analyzer{});
  EXPECT_EQ(idx.doc_freq("haaland"), 1U);
  EXPECT_EQ(idx.doc_freq("city"), 1U);
}

TEST(BuildIndexTest, CommittedCorpusInvariants) {
  const corpus::Corpus c = corpus::load_tsv_file("data/corpus/pl-2025-26.tsv");
  const InvertedIndex idx = build_index(c, text::Analyzer{});
  EXPECT_EQ(idx.stats().doc_count, c.size());
  // Every postings list is strictly increasing in DocId with tf >= 1.
  for (const auto* term : {"anfield", "late", "winner", "haaland", "goal", "arsenal"}) {
    const auto list = idx.postings(term);
    ASSERT_FALSE(list.empty()) << term;
    EXPECT_TRUE(std::ranges::adjacent_find(
                    list, [](const Posting& a, const Posting& b) { return a.doc >= b.doc; }) ==
                list.end())
        << term;
    EXPECT_TRUE(std::ranges::all_of(list, [](const Posting& p) { return p.tf >= 1; })) << term;
  }
}

}  // namespace
}  // namespace gaffer::index
