#include "eval/metrics.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "common/error.hpp"

namespace gaffer::eval {

namespace {

// Splits "a\tb\tc" into fields; skips blank and '#' lines via the caller.
std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    const auto tab = line.find('\t', start);
    out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
    if (tab == std::string::npos) {
      return out;
    }
    start = tab + 1;
  }
}

template <typename OnFields>
void read_tsv(const std::filesystem::path& path, std::size_t fields, OnFields on_fields) {
  std::ifstream in(path);
  if (!in) {
    throw IoError("cannot open '" + path.string() + "'");
  }
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto f = split_tabs(line);
    if (f.size() != fields) {
      throw ParseError(path.string(), line_no, "expected " + std::to_string(fields) + " fields");
    }
    on_fields(f);
  }
}

std::size_t hits_in_top(const std::vector<std::string>& ranked,
                        const std::set<std::string>& relevant, std::size_t k) {
  const std::size_t n = std::min(k, ranked.size());
  return static_cast<std::size_t>(
      std::count_if(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(n),
                    [&](const std::string& key) { return relevant.contains(key); }));
}

}  // namespace

std::vector<EvalQuery> load_queries(const std::filesystem::path& path) {
  std::vector<EvalQuery> out;
  read_tsv(path, 3, [&](const std::vector<std::string>& f) { out.push_back({f[0], f[1], f[2]}); });
  return out;
}

Qrels load_qrels(const std::filesystem::path& path) {
  Qrels out;
  read_tsv(path, 2, [&](const std::vector<std::string>& f) { out[f[0]].insert(f[1]); });
  return out;
}

double precision_at(const std::vector<std::string>& ranked, const std::set<std::string>& relevant,
                    std::size_t k) {
  return k == 0 ? 0.0
                : static_cast<double>(hits_in_top(ranked, relevant, k)) / static_cast<double>(k);
}

double recall_at(const std::vector<std::string>& ranked, const std::set<std::string>& relevant,
                 std::size_t k) {
  return relevant.empty() ? 0.0
                          : static_cast<double>(hits_in_top(ranked, relevant, k)) /
                                static_cast<double>(relevant.size());
}

double reciprocal_rank(const std::vector<std::string>& ranked,
                       const std::set<std::string>& relevant) {
  for (std::size_t i = 0; i < ranked.size(); ++i) {
    if (relevant.contains(ranked[i])) {
      return 1.0 / static_cast<double>(i + 1);
    }
  }
  return 0.0;
}

}  // namespace gaffer::eval
