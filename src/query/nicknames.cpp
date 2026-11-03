#include "query/nicknames.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <istream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/error.hpp"
#include "text/tokenizer.hpp"

namespace gaffer::query {

namespace {

std::vector<std::string> raw_terms(std::string_view text) {
  std::vector<std::string> out;
  for (auto& tok : text::tokenize(text)) {
    out.push_back(std::move(tok.term));
  }
  return out;
}

}  // namespace

NicknameExpander::NicknameExpander(
    const std::vector<std::pair<std::string, std::string>>& entries) {
  for (const auto& [alias, expansion] : entries) {
    auto tokens = raw_terms(alias);
    if (tokens.empty()) {
      throw Error("nickname alias '" + alias + "' has no searchable words");
    }
    const bool duplicate =
        std::ranges::any_of(entries_, [&](const Entry& e) { return e.tokens == tokens; });
    if (duplicate) {
      throw Error("duplicate nickname alias '" + alias + "'");
    }
    entries_.push_back(Entry{std::move(tokens), Expansion{alias, expansion}});
  }
  // Longest first, so "man utd" is tried before a hypothetical "man".
  // stable_sort keeps file order among equal lengths: deterministic.
  std::ranges::stable_sort(entries_, std::greater<>{},
                           [](const Entry& e) { return e.tokens.size(); });
}

NicknameExpander NicknameExpander::load_tsv(std::istream& in, std::string_view source) {
  const std::string src(source);
  std::vector<std::pair<std::string, std::string>> entries;
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto tab = line.find('\t');
    if (tab == std::string::npos || tab == 0 || tab + 1 == line.size() ||
        line.find('\t', tab + 1) != std::string::npos) {
      throw ParseError(src, line_no, "expected 'alias<TAB>expansion'");
    }
    entries.emplace_back(line.substr(0, tab), line.substr(tab + 1));
  }
  try {
    return NicknameExpander(entries);
  } catch (const ParseError&) {
    throw;
  } catch (const Error& e) {
    throw ParseError(src, line_no, e.what());
  }
}

NicknameExpander NicknameExpander::load_tsv_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw IoError("cannot open nicknames file '" + path.string() + "'");
  }
  return load_tsv(in, path.string());
}

std::vector<Expansion> NicknameExpander::find(std::string_view text) const {
  const std::vector<std::string> terms = raw_terms(text);
  std::vector<Expansion> found;
  std::size_t i = 0;
  while (i < terms.size()) {
    const auto hit = std::ranges::find_if(entries_, [&](const Entry& e) {
      return i + e.tokens.size() <= terms.size() &&
             std::equal(e.tokens.begin(), e.tokens.end(),
                        terms.begin() + static_cast<std::ptrdiff_t>(i));
    });
    if (hit == entries_.end()) {
      ++i;
      continue;
    }
    found.push_back(hit->expansion);
    i += hit->tokens.size();  // consumed: these tokens can't start another alias
  }
  return found;
}

}  // namespace gaffer::query
