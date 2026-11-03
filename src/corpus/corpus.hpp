#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "corpus/document.hpp"

namespace gaffer::corpus {

// An immutable, in-memory collection of documents. Position = DocId.
class Corpus {
 public:
  Corpus() = default;
  // Throws gaffer::Error if keys are duplicated or there are too many docs for DocId.
  explicit Corpus(std::vector<Document> docs);

  [[nodiscard]] std::size_t size() const noexcept { return docs_.size(); }
  [[nodiscard]] bool empty() const noexcept { return docs_.empty(); }
  // Precondition: id < size(). Checked with assert in debug builds.
  [[nodiscard]] const Document& operator[](DocId id) const noexcept;
  [[nodiscard]] std::span<const Document> documents() const noexcept { return docs_; }
  [[nodiscard]] std::optional<DocId> find(std::string_view key) const;

 private:
  std::vector<Document> docs_;
  std::unordered_map<std::string, DocId> by_key_;
};

// 64-bit FNV-1a hash over every field of every document, in order. Stored in
// index files so an index can't silently be used with a different corpus.
// Fast and well distributed for this purpose; not cryptographic.
[[nodiscard]] std::uint64_t fingerprint(const Corpus& corpus) noexcept;

// Writes a corpus in the TSV format load_tsv reads (header, escaped fields).
// Used to write per-shard corpus files. Throws gaffer::IoError on failure.
void write_tsv_file(const Corpus& corpus, const std::filesystem::path& path);

// The corpus writer's escaping: '\\' -> "\\\\", TAB -> "\\t", LF -> "\\n".
[[nodiscard]] std::string escape_field(std::string_view field);

// Reverses the corpus writer's escaping: "\\t" -> TAB, "\\n" -> LF, "\\\\" -> '\'.
// Returns std::nullopt on a dangling or unknown escape.
[[nodiscard]] std::optional<std::string> unescape_field(std::string_view field);

// Parses the TSV corpus format (tools/corpusgen/corpusgen/tsv.py):
//   "# gaffer-corpus v1" header, then "key\ttype\ttitle\tbody" per line.
// `source` names the input in error messages. Throws gaffer::ParseError with a
// line number on malformed input.
[[nodiscard]] Corpus load_tsv(std::istream& in, std::string_view source);

// Opens and parses a corpus file. Throws gaffer::IoError if it can't be opened.
[[nodiscard]] Corpus load_tsv_file(const std::filesystem::path& path);

// Loads several corpus files as one corpus, in order (DocIds continue across
// files). A key appearing in two files is an error (gaffer::Error).
[[nodiscard]] Corpus load_tsv_files(std::span<const std::filesystem::path> paths);

}  // namespace gaffer::corpus
