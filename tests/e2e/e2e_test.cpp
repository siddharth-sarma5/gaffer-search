// End-to-end tests: corpus file -> analyzer -> index -> BM25 -> top-k.
//
// 1. A hand-written fixture corpus with rankings we can reason about.
// 2. The Milestone 1 acceptance queries on the real 2025-26 corpus.
// 3. A golden file of top-5 results for a fixed query set. This catches any
//    unintended ranking change. Regenerate it deliberately with
//      GAFFER_UPDATE_GOLDEN=1 ctest -R GoldenTest
//    and review the diff before committing.

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "query/searcher.hpp"
#include "text/analyzer.hpp"

namespace gaffer {
namespace {

// Bundles a corpus with its index and searcher (Searcher borrows the index).
struct Engine {
  explicit Engine(const std::string& path)
      : corpus(corpus::load_tsv_file(path)),
        index(index::build_index(corpus, text::Analyzer{})),
        searcher(index) {}

  std::vector<std::string> top_keys(const std::string& q, std::size_t k) const {
    std::vector<std::string> keys;
    for (const auto& hit : searcher.search(q, k).hits) {
      keys.push_back(corpus[hit.doc].key);
    }
    return keys;
  }

  corpus::Corpus corpus;
  index::InvertedIndex index;
  query::Searcher searcher;
};

using Keys = std::vector<std::string>;

// --- 1. fixture corpus -----------------------------------------------------

class FixtureTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    engine_ = std::make_unique<Engine>("tests/fixtures/mini_corpus.tsv");
  }
  static void TearDownTestSuite() { engine_.reset(); }
  static std::unique_ptr<Engine> engine_;
};
std::unique_ptr<Engine> FixtureTest::engine_;

TEST_F(FixtureTest, AccentFoldingMatchesUnaccentedQuery) {
  // Player page: "odegaard" in title and body of a 5-term doc beats one mention in a match.
  EXPECT_EQ(engine_->top_keys("odegaard", 10), (Keys{"p-odegaard", "m-emirates"}));
  EXPECT_EQ(engine_->top_keys("ØDEGAARD", 10), (Keys{"p-odegaard", "m-emirates"}));
}

TEST_F(FixtureTest, StemmingMatchesSingularAndPlural) {
  EXPECT_EQ(engine_->top_keys("penalty", 10), (Keys{"p-salah", "m-emirates"}));
  EXPECT_EQ(engine_->top_keys("penalties", 10), engine_->top_keys("penalty", 10));
}

TEST_F(FixtureTest, MultiTermQueryRanksDocumentMatchingMostTermsFirst) {
  EXPECT_EQ(engine_->top_keys("salah late winner", 1), (Keys{"m-anfield"}));
}

TEST_F(FixtureTest, SingleMatchAndNoMatch) {
  EXPECT_EQ(engine_->top_keys("toffees", 10), (Keys{"c-everton"}));
  EXPECT_TRUE(engine_->top_keys("the at a", 10).empty());
  EXPECT_TRUE(engine_->top_keys("mbappe", 10).empty());
}

// --- 2 & 3. real corpus ----------------------------------------------------

class RealCorpusTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { engine_ = std::make_unique<Engine>("data/corpus/pl-2025-26.tsv"); }
  static void TearDownTestSuite() { engine_.reset(); }
  static std::unique_ptr<Engine> engine_;
};
std::unique_ptr<Engine> RealCorpusTest::engine_;

TEST_F(RealCorpusTest, AcceptanceLateWinnerAtAnfield) {
  // There are exactly four Anfield late-winner matches in 2025-26; they must be the top 4.
  auto top = engine_->top_keys("late winner at anfield", 4);
  std::ranges::sort(top);
  EXPECT_EQ(top,
            (Keys{"match-2025-08-15-liverpool-bournemouth", "match-2025-08-31-liverpool-arsenal",
                  "match-2025-10-19-liverpool-man-united", "match-2026-02-08-liverpool-man-city"}));
}

TEST_F(RealCorpusTest, AcceptancePalmerHatTrick) {
  const auto top = engine_->top_keys("palmer hat-trick", 3);
  EXPECT_NE(std::ranges::find(top, "match-2026-02-07-wolves-chelsea"), top.end());
}

TEST_F(RealCorpusTest, AcceptanceHaalandBrace) {
  // Every top-5 hit is Haaland's page or a match where he scored exactly twice.
  for (const auto& key : engine_->top_keys("haaland brace", 5)) {
    const auto& doc = engine_->corpus[*engine_->corpus.find(key)];
    const bool ok = key == "player-erling-haaland" ||
                    doc.body.find("Erling Haaland scored twice") != std::string::npos;
    EXPECT_TRUE(ok) << key;
  }
}

TEST_F(RealCorpusTest, PhraseQueriesOnlyReturnAdjacentMatches) {
  // Unquoted, "north london derby" also ranks North West derbies (golden file).
  // Quoted, it returns exactly the two North London derbies.
  auto nld = engine_->top_keys("\"north london derby\"", 10);
  std::ranges::sort(nld);
  EXPECT_EQ(nld,
            (Keys{"match-2025-11-23-arsenal-tottenham", "match-2026-02-22-tottenham-arsenal"}));

  for (const auto& key : engine_->top_keys("\"late winner\"", 100)) {
    const auto& doc = engine_->corpus[*engine_->corpus.find(key)];
    EXPECT_NE(doc.body.find("late winner"), std::string::npos) << key;
  }
}

// Queries covering every document type and the main derived tags.
const std::vector<std::string> kGoldenQueries{
    "late winner at anfield",
    "palmer hat-trick",
    "haaland brace",
    "gunners",
    "champions",
    "relegated",
    "north london derby",
    "sent off second yellow",
    "comeback win",
    "goalless draw",
    "own goal",
    "stoppage time penalty",
    "odegaard",
    "stadium of light",
    "biggest win",
};
constexpr const char* kGoldenPath = "tests/golden/pl-2025-26-top5.txt";

std::string render_golden(const Engine& e) {
  std::ostringstream out;
  out << "# Top-5 results per query on data/corpus/pl-2025-26.tsv (BM25 k1=1.2 b=0.75).\n"
         "# Generated by tests/e2e/e2e_test.cpp; regenerate with GAFFER_UPDATE_GOLDEN=1.\n";
  for (const auto& q : kGoldenQueries) {
    out << "\nquery: " << q << "\n";
    for (const auto& hit : e.searcher.search(q, 5).hits) {
      // 4 decimals: stable across compilers, still catches real scoring changes.
      out << std::format("  {:.4f}  {}\n", hit.score, e.corpus[hit.doc].key);
    }
  }
  return out.str();
}

TEST_F(RealCorpusTest, GoldenTop5) {
  const std::string actual = render_golden(*engine_);
  if (std::getenv("GAFFER_UPDATE_GOLDEN") != nullptr) {  // NOLINT(concurrency-mt-unsafe)
    std::ofstream(kGoldenPath, std::ios::binary) << actual;
    GTEST_SKIP() << "golden file rewritten: " << kGoldenPath;
  }
  std::ifstream in(kGoldenPath, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(in) << "missing " << kGoldenPath << "; run with GAFFER_UPDATE_GOLDEN=1";
  // Sized read rather than istreambuf_iterator: GCC 13 at -O3 reports a false
  // -Wnull-dereference inside libstdc++'s streambuf for the iterator version.
  std::string expected(static_cast<std::size_t>(in.tellg()), '\0');
  in.seekg(0);
  in.read(expected.data(), static_cast<std::streamsize>(expected.size()));
  EXPECT_EQ(actual, expected) << "ranking changed; if intended, regenerate the golden file";
}

}  // namespace
}  // namespace gaffer
