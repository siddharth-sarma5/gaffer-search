#include "corpus/corpus.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/error.hpp"
#include "common/types.hpp"
#include "corpus/document.hpp"

namespace gaffer::corpus {

namespace {

constexpr std::string_view kHeader = "# gaffer-corpus v1";
constexpr std::size_t kFieldCount = 4;

// Splits on real TAB characters. Escaped tabs ("\\t") are two ordinary bytes,
// so a plain split is correct, which is why the format escapes them.
std::vector<std::string_view> split_tabs(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(start));
      return fields;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
}

}  // namespace

Corpus::Corpus(std::vector<Document> docs) : docs_(std::move(docs)) {
  if (docs_.size() > std::numeric_limits<DocId>::max()) {
    throw Error("corpus has more documents than DocId can address");
  }
  by_key_.reserve(docs_.size());
  for (std::size_t i = 0; i < docs_.size(); ++i) {
    const auto [it, inserted] = by_key_.emplace(docs_[i].key, static_cast<DocId>(i));
    if (!inserted) {
      throw Error("duplicate document key '" + docs_[i].key + "'");
    }
  }
}

const Document& Corpus::operator[](DocId id) const noexcept {
  assert(id < docs_.size());
  return docs_[id];
}

std::optional<DocId> Corpus::find(std::string_view key) const {
  // C++20's heterogeneous unordered lookup needs a custom transparent hash;
  // building a std::string here is simpler, and find() isn't on a hot path.
  const auto it = by_key_.find(std::string(key));
  if (it == by_key_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::uint64_t fingerprint(const Corpus& corpus) noexcept {
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t h = kOffsetBasis;
  auto mix = [&](std::string_view s) {
    for (const char c : s) {
      h ^= static_cast<unsigned char>(c);
      h *= kPrime;
    }
    h ^= 0xFFU;  // field separator: ("ab","c") and ("a","bc") hash differently
    h *= kPrime;
  };
  for (const Document& d : corpus.documents()) {
    mix(d.key);
    mix(to_string(d.type));
    mix(d.title);
    mix(d.body);
  }
  return h;
}

std::optional<std::string> unescape_field(std::string_view field) {
  std::string out;
  out.reserve(field.size());
  for (std::size_t i = 0; i < field.size(); ++i) {
    const char c = field[i];
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (++i == field.size()) {
      return std::nullopt;  // dangling backslash at end of field
    }
    switch (field[i]) {
      case 't':
        out.push_back('\t');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case '\\':
        out.push_back('\\');
        break;
      default:
        return std::nullopt;
    }
  }
  return out;
}

Corpus load_tsv(std::istream& in, std::string_view source) {
  const std::string src(source);
  std::string line;
  std::size_t line_no = 0;

  if (!std::getline(in, line)) {
    throw ParseError(src, 1, "empty input; expected header '" + std::string(kHeader) + "'");
  }
  ++line_no;
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();  // tolerate CRLF line endings from Windows checkouts
  }
  if (line != kHeader) {
    throw ParseError(src, line_no, "expected header '" + std::string(kHeader) + "'");
  }

  std::vector<Document> docs;
  std::unordered_map<std::string, std::size_t> first_seen;  // key -> line number
  while (std::getline(in, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    const auto fields = split_tabs(line);
    if (fields.size() != kFieldCount) {
      throw ParseError(src, line_no,
                       "expected 4 tab-separated fields, got " + std::to_string(fields.size()));
    }

    std::array<std::string, kFieldCount> values;
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      auto value = unescape_field(fields[i]);
      if (!value) {
        throw ParseError(src, line_no, "invalid escape sequence in field " + std::to_string(i + 1));
      }
      values[i] = std::move(*value);
    }
    if (values[0].empty()) {
      throw ParseError(src, line_no, "empty document key");
    }
    if (const auto [it, inserted] = first_seen.emplace(values[0], line_no); !inserted) {
      throw ParseError(src, line_no,
                       "duplicate document key '" + values[0] + "' (first seen on line " +
                           std::to_string(it->second) + ")");
    }
    const auto type = parse_doc_type(values[1]);
    if (!type) {
      throw ParseError(src, line_no, "unknown document type '" + values[1] + "'");
    }
    docs.push_back(
        Document{std::move(values[0]), *type, std::move(values[2]), std::move(values[3])});
  }
  if (in.bad()) {
    throw IoError("read error in " + src);
  }

  return Corpus(std::move(docs));
}

Corpus load_tsv_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw IoError("cannot open corpus file '" + path.string() + "'");
  }
  return load_tsv(in, path.string());
}

}  // namespace gaffer::corpus
