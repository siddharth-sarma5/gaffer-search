#include "corpus/corpus.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.hpp"
#include "corpus/document.hpp"

namespace gaffer::corpus {
namespace {

Corpus load(const std::string& text) {
  std::istringstream in(text);
  return load_tsv(in, "test.tsv");
}

// Asserts that loading `text` throws a ParseError on `line` whose message contains `needle`.
void expect_parse_error(const std::string& text, std::size_t line, const std::string& needle) {
  try {
    (void)load(text);
    FAIL() << "expected ParseError";
  } catch (const ParseError& e) {
    EXPECT_EQ(e.line(), line) << e.what();
    EXPECT_NE(std::string(e.what()).find(needle), std::string::npos) << e.what();
  }
}

const std::string kHeader = "# gaffer-corpus v1\n";

TEST(UnescapeTest, DecodesAllEscapes) {
  EXPECT_EQ(unescape_field(R"(a\tb\nc\\d)"), std::string("a\tb\nc\\d"));
  EXPECT_EQ(unescape_field("plain"), std::string("plain"));
  EXPECT_EQ(unescape_field(""), std::string());
}

TEST(UnescapeTest, RejectsDanglingAndUnknownEscapes) {
  EXPECT_FALSE(unescape_field(R"(abc\)").has_value());
  EXPECT_FALSE(unescape_field(R"(a\xb)").has_value());
}

TEST(DocTypeTest, RoundTrips) {
  for (const auto type : {DocType::kMatch, DocType::kClub, DocType::kPlayer, DocType::kSeason}) {
    EXPECT_EQ(parse_doc_type(to_string(type)), type);
  }
  EXPECT_FALSE(parse_doc_type("stadium").has_value());
}

TEST(LoadTsvTest, LoadsDocumentsInOrderWithDenseIds) {
  const Corpus c = load(kHeader +
                        "m1\tmatch\tLiverpool 4-2 Bournemouth\tAt Anfield.\n"
                        "p1\tplayer\tMohamed Salah\tLiverpool.\n");
  ASSERT_EQ(c.size(), 2U);
  EXPECT_EQ(c[0].key, "m1");
  EXPECT_EQ(c[0].type, DocType::kMatch);
  EXPECT_EQ(c[0].title, "Liverpool 4-2 Bournemouth");
  EXPECT_EQ(c[1].body, "Liverpool.");
  EXPECT_EQ(c.find("p1"), DocId{1});
  EXPECT_FALSE(c.find("nope").has_value());
}

TEST(LoadTsvTest, UnescapesFieldsAndToleratesCrlfAndBlankLines) {
  const Corpus c = load("# gaffer-corpus v1\r\n\nk\tclub\tA\\tB\tline1\\nline2\r\n\n");
  ASSERT_EQ(c.size(), 1U);
  EXPECT_EQ(c[0].title, "A\tB");
  EXPECT_EQ(c[0].body, "line1\nline2");
}

TEST(LoadTsvTest, HeaderOnlyGivesEmptyCorpus) { EXPECT_TRUE(load(kHeader).empty()); }

TEST(LoadTsvTest, Errors) {
  expect_parse_error("", 1, "empty input");
  expect_parse_error("id\ttype\ttitle\tbody\n", 1, "expected header");
  expect_parse_error(kHeader + "k\tmatch\ttitle\n", 2, "got 3");
  expect_parse_error(kHeader + "k\tmatch\tt\tb\textra\n", 2, "got 5");
  expect_parse_error(kHeader + "k\tstadium\tt\tb\n", 2, "unknown document type 'stadium'");
  expect_parse_error(kHeader + "\tmatch\tt\tb\n", 2, "empty document key");
  expect_parse_error(kHeader + "k\tmatch\tt\tbad\\q\n", 2, "invalid escape sequence in field 4");
  expect_parse_error(kHeader + "k\tmatch\tt\tb\nj\tclub\tt\tb\nk\tclub\tt\tb\n", 4,
                     "duplicate document key 'k' (first seen on line 2)");
}

TEST(LoadTsvFileTest, MissingFileIsIoError) {
  EXPECT_THROW((void)load_tsv_file("does/not/exist.tsv"), IoError);
}

// Tests run from the repository root (see tests/CMakeLists.txt WORKING_DIRECTORY).
TEST(LoadTsvFileTest, LoadsTheCommittedCorpus) {
  const Corpus c = load_tsv_file("data/corpus/pl-2025-26.tsv");
  EXPECT_EQ(c.size(), 938U);
  const auto id = c.find("match-2025-08-15-liverpool-bournemouth");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(c[*id].type, DocType::kMatch);
  EXPECT_NE(c[*id].body.find("late winner"), std::string::npos);
}

TEST(LoadTsvFilesTest, ConcatenatesInOrderAndRejectsDuplicateKeys) {
  const std::vector<std::filesystem::path> both{"tests/fixtures/mini_corpus.tsv",
                                                "data/corpus/pl-2025-26.tsv"};
  const Corpus c = load_tsv_files(both);
  EXPECT_EQ(c.size(), 6U + 938U);
  EXPECT_EQ(c[0].key, "m-anfield");
  EXPECT_EQ(c.find("season-2025-26"), DocId{6});
  const std::vector<std::filesystem::path> twice{"tests/fixtures/mini_corpus.tsv",
                                                 "tests/fixtures/mini_corpus.tsv"};
  EXPECT_THROW((void)load_tsv_files(twice), Error);
}

TEST(LoadTsvFileTest, LoadsTheHistoryCorpus) {
  const Corpus c = load_tsv_file("data/corpus/pl-history.tsv");
  EXPECT_EQ(c.size(), 12786U);
  EXPECT_TRUE(c.find("hist-1996-04-03-liverpool-newcastle-united").has_value());
}

}  // namespace
}  // namespace gaffer::corpus
