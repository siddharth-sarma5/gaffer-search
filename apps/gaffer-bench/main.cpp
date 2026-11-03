// gaffer-bench: measures indexing throughput, index size and query latency.
//
//   gaffer-bench --corpus FILE [--corpus FILE ...] [--queries bench/queries.tsv]
//                [--runs 5] [--reps 200] [--label NAME] [--json OUT]
//
// Method (what the README numbers mean):
//  * Indexing: build_index over the whole corpus, `runs` times; the median is
//    reported as docs/s and MB/s of indexed text (title + body bytes).
//  * Serialise / load: serialize_index and deserialize_index of the full index,
//    in memory (no disk I/O, so it's codec speed, not SSD speed); median of `runs`.
//  * Size: compressed postings vs the same postings as fixed-width uint32.
//  * Latency: each query in the log is run `warmup` times (warm caches and
//    branch predictors), then `reps` rounds over the whole log. Every
//    individual search(q, k=10) call is timed with steady_clock. Percentiles
//    use the nearest-rank method over all samples. Single thread.
// Numbers are only meaningful from a Release build; the tool warns otherwise.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "index/index_file.hpp"
#include "index/inverted_index.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "text/analyzer.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  std::vector<std::filesystem::path> corpora;
  std::string queries = "bench/queries.tsv";
  std::string nicknames = "data/nicknames.tsv";
  std::string label = "bench";
  std::optional<std::string> json;
  int runs = 5;
  int reps = 200;
  int warmup = 3;
  std::size_t k = 10;
};

struct Query {
  std::string category;
  std::string text;
};

struct Percentiles {
  double p50 = 0, p90 = 0, p99 = 0, max = 0, mean = 0;
  std::size_t samples = 0;
};

double to_us(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }

double median(std::vector<double> v) {
  std::ranges::sort(v);
  return v[v.size() / 2];
}

// Nearest-rank percentile: the smallest sample >= p% of all samples.
Percentiles percentiles(std::vector<double> us) {
  Percentiles p;
  if (us.empty()) {
    return p;
  }
  std::ranges::sort(us);
  auto rank = [&](double pct) {
    const auto idx =
        static_cast<std::size_t>(std::ceil(pct / 100.0 * static_cast<double>(us.size())));
    return us[std::clamp<std::size_t>(idx, 1, us.size()) - 1];
  };
  p.p50 = rank(50);
  p.p90 = rank(90);
  p.p99 = rank(99);
  p.max = us.back();
  double sum = 0;
  for (const double x : us) {
    sum += x;
  }
  p.mean = sum / static_cast<double>(us.size());
  p.samples = us.size();
  return p;
}

std::vector<Query> load_queries(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw gaffer::IoError("cannot open query log '" + path + "'");
  }
  std::vector<Query> out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto tab = line.find('\t');
    if (tab == std::string::npos) {
      throw gaffer::Error("query log line without a category: " + line);
    }
    out.push_back({line.substr(0, tab), line.substr(tab + 1)});
  }
  return out;
}

std::string cpu_model() {
  std::ifstream in("/proc/cpuinfo");
  std::string line;
  while (std::getline(in, line)) {
    if (line.starts_with("model name")) {
      return line.substr(line.find(':') + 2);
    }
  }
  return "unknown";
}

Options parse_args(std::span<char*> args) {
  Options o;
  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string_view a = args[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= args.size()) {
        throw gaffer::Error(std::format("{} needs a value", a));
      }
      return args[++i];
    };
    if (a == "--corpus") {
      o.corpora.emplace_back(value());
    } else if (a == "--queries") {
      o.queries = value();
    } else if (a == "--nicknames") {
      o.nicknames = value();
    } else if (a == "--label") {
      o.label = value();
    } else if (a == "--json") {
      o.json = value();
    } else if (a == "--runs") {
      o.runs = std::max(1, std::stoi(value()));
    } else if (a == "--reps") {
      o.reps = std::max(1, std::stoi(value()));
    } else {
      throw gaffer::Error(std::format("unknown option '{}'", a));
    }
  }
  if (o.corpora.empty()) {
    o.corpora.emplace_back("data/corpus/pl-2025-26.tsv");
  }
  return o;
}

int run(const Options& opt) {
#ifndef NDEBUG
  std::cerr << "WARNING: this is not a Release build; timings are not representative.\n";
  constexpr std::string_view kBuild = "debug";
#else
  constexpr std::string_view kBuild = "release";
#endif
  const auto corpus = gaffer::corpus::load_tsv_files(opt.corpora);
  std::size_t text_bytes = 0;
  for (const auto& d : corpus.documents()) {
    text_bytes += d.title.size() + 1 + d.body.size();
  }

  // 1. Indexing throughput.
  std::vector<double> build_ms;
  gaffer::index::InvertedIndex index;
  for (int r = 0; r < opt.runs; ++r) {
    const auto t0 = Clock::now();
    index = gaffer::index::build_index(corpus, gaffer::text::Analyzer{});
    build_ms.push_back(to_us(Clock::now() - t0) / 1000.0);
  }
  const double build = median(build_ms);
  const double docs_per_s = static_cast<double>(corpus.size()) / (build / 1000.0);
  const double mb_per_s = static_cast<double>(text_bytes) / 1e6 / (build / 1000.0);

  // 2. Serialise / load, and sizes.
  gaffer::index::IndexFileStats sizes;
  std::vector<std::uint8_t> bytes;
  std::vector<double> ser_ms;
  std::vector<double> load_ms;
  for (int r = 0; r < opt.runs; ++r) {
    auto t0 = Clock::now();
    bytes = gaffer::index::serialize_index(index, gaffer::corpus::fingerprint(corpus), &sizes);
    ser_ms.push_back(to_us(Clock::now() - t0) / 1000.0);
    t0 = Clock::now();
    const auto loaded = gaffer::index::deserialize_index(bytes);
    load_ms.push_back(to_us(Clock::now() - t0) / 1000.0);
    if (loaded.index.term_count() != index.term_count()) {
      throw gaffer::Error("round trip mismatch");
    }
  }

  // 3. Query latency.
  const auto queries = load_queries(opt.queries);
  gaffer::query::NicknameExpander expander;
  if (std::filesystem::exists(opt.nicknames)) {
    expander = gaffer::query::NicknameExpander::load_tsv_file(opt.nicknames);
  }
  const gaffer::query::Searcher searcher(index, {}, &expander);
  std::size_t sink = 0;  // consumed below so the compiler can't drop the searches
  for (int w = 0; w < opt.warmup; ++w) {
    for (const auto& q : queries) {
      sink += searcher.search(q.text, opt.k).hits.size();
    }
  }
  std::vector<double> all_us;
  std::map<std::string, std::vector<double>> by_category;
  std::size_t matched_total = 0;
  for (int rep = 0; rep < opt.reps; ++rep) {
    for (const auto& q : queries) {
      const auto t0 = Clock::now();
      const auto r = searcher.search(q.text, opt.k);
      const double us = to_us(Clock::now() - t0);
      sink += r.hits.size();
      if (rep == 0) {
        matched_total += r.matched;
      }
      all_us.push_back(us);
      by_category[q.category].push_back(us);
    }
  }
  const Percentiles overall = percentiles(all_us);

  // Report.
  const auto& s = index.stats();
  std::cout << std::format(
      "gaffer-bench [{}] ({} build)\n"
      "  machine:  {} ({} hw threads), compiler {}\n"
      "  corpus:   {} docs, {:.2f} MB text, {} tokens, {} terms, {} postings\n"
      "  indexing: median {:.1f} ms of {} runs  -> {:.0f} docs/s, {:.2f} MB/s\n"
      "  size:     postings {} B fixed-width -> {} B compressed ({:.1f}%); file {} B\n"
      "  codec:    serialise median {:.1f} ms, load median {:.1f} ms (in memory)\n"
      "  queries:  {} queries x {} reps = {} samples, k={}, {} matched docs/round\n"
      "  latency:  p50 {:.1f} us  p90 {:.1f} us  p99 {:.1f} us  max {:.1f} us  mean {:.1f} us\n",
      opt.label, kBuild, cpu_model(), std::thread::hardware_concurrency(), __VERSION__,
      corpus.size(), static_cast<double>(text_bytes) / 1e6, s.total_length, index.term_count(),
      index.posting_count(), build, opt.runs, docs_per_s, mb_per_s,
      sizes.fixed_width_postings_bytes, sizes.postings_bytes,
      100.0 * static_cast<double>(sizes.postings_bytes) /
          static_cast<double>(sizes.fixed_width_postings_bytes),
      sizes.file_bytes, median(ser_ms), median(load_ms), queries.size(), opt.reps, overall.samples,
      opt.k, matched_total, overall.p50, overall.p90, overall.p99, overall.max, overall.mean);
  for (const auto& [cat, us] : by_category) {
    const auto p = percentiles(us);
    std::cout << std::format("    {:<9} p50 {:7.1f} us  p99 {:7.1f} us\n", cat, p.p50, p.p99);
  }

  if (opt.json) {
    std::ofstream out(*opt.json);
    out << std::format(
        "{{\n  \"label\": \"{}\",\n  \"build\": \"{}\",\n  \"cpu\": \"{}\",\n"
        "  \"hw_threads\": {},\n  \"compiler\": \"{}\",\n"
        "  \"docs\": {},\n  \"text_bytes\": {},\n  \"tokens\": {},\n  \"terms\": {},\n"
        "  \"postings\": {},\n  \"index_build_ms_median\": {:.3f},\n  \"index_runs\": {},\n"
        "  \"docs_per_s\": {:.1f},\n  \"mb_per_s\": {:.3f},\n"
        "  \"postings_fixed_width_bytes\": {},\n  \"postings_compressed_bytes\": {},\n"
        "  \"index_file_bytes\": {},\n  \"serialize_ms_median\": {:.3f},\n"
        "  \"load_ms_median\": {:.3f},\n  \"queries\": {},\n  \"reps\": {},\n  \"k\": {},\n"
        "  \"latency_us\": {{\"p50\": {:.2f}, \"p90\": {:.2f}, \"p99\": {:.2f}, \"max\": {:.2f}, "
        "\"mean\": {:.2f}, \"samples\": {}}},\n  \"by_category_us\": {{",
        opt.label, kBuild, cpu_model(), std::thread::hardware_concurrency(), __VERSION__,
        corpus.size(), text_bytes, s.total_length, index.term_count(), index.posting_count(), build,
        opt.runs, docs_per_s, mb_per_s, sizes.fixed_width_postings_bytes, sizes.postings_bytes,
        sizes.file_bytes, median(ser_ms), median(load_ms), queries.size(), opt.reps, opt.k,
        overall.p50, overall.p90, overall.p99, overall.max, overall.mean, overall.samples);
    bool first = true;
    for (const auto& [cat, us] : by_category) {
      const auto p = percentiles(us);
      out << std::format("{}\n    \"{}\": {{\"p50\": {:.2f}, \"p99\": {:.2f}}}", first ? "" : ",",
                         cat, p.p50, p.p99);
      first = false;
    }
    out << "\n  }\n}\n";
  }
  return sink == 0 ? 1 : 0;  // every query log returns something; 0 hits means a broken setup
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(parse_args(std::span<char*>(argv, static_cast<std::size_t>(argc))));
  } catch (const std::exception& e) {
    std::cerr << "gaffer-bench: " << e.what() << "\n";
    return 1;
  }
}
