// gaffer-eval: relevance evaluation on the 2025-26 corpus.
//
//   gaffer-eval [--queries eval/queries.tsv] [--qrels eval/qrels.tsv]
//               [--corpus data/corpus/pl-2025-26.tsv] [--json OUT]
//
// Runs every labelled query under several configurations and reports mean
// precision@5, precision@10, recall@10 and MRR, then a per-query table for the
// default configuration (the one the CLI uses). Judgements come from
// eval/make_qrels.py, which defines relevance from structured match facts,
// never from engine output.

#include <cstddef>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "eval/metrics.hpp"
#include "index/inverted_index.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "ranking/bm25.hpp"
#include "text/analyzer.hpp"

namespace {

constexpr std::size_t kDepth = 100;  // ranks retrieved per query (MRR looks this deep)

struct Config {
  std::string name;
  gaffer::ranking::Bm25Params bm25;
  std::optional<double> expansion;  // nullopt = no nickname expansion
};

struct Scores {
  double p5 = 0, p10 = 0, r10 = 0, rr = 0;
};

struct Options {
  std::string queries = "eval/queries.tsv";
  std::string qrels = "eval/qrels.tsv";
  std::string corpus = "data/corpus/pl-2025-26.tsv";
  std::string nicknames = "data/nicknames.tsv";
  std::optional<std::string> json;
};

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
    if (a == "--queries") {
      o.queries = value();
    } else if (a == "--qrels") {
      o.qrels = value();
    } else if (a == "--corpus") {
      o.corpus = value();
    } else if (a == "--json") {
      o.json = value();
    } else {
      throw gaffer::Error(std::format("unknown option '{}'", a));
    }
  }
  return o;
}

int run(const Options& opt) {
  const auto corpus = gaffer::corpus::load_tsv_file(opt.corpus);
  const auto index = gaffer::index::build_index(corpus, gaffer::text::Analyzer{});
  const auto queries = gaffer::eval::load_queries(opt.queries);
  const auto qrels = gaffer::eval::load_qrels(opt.qrels);
  gaffer::query::NicknameExpander expander =
      gaffer::query::NicknameExpander::load_tsv_file(opt.nicknames);

  // First entry = the CLI default. The others vary one thing at a time.
  const std::vector<Config> configs{
      {"default (b=0.75, nicknames x0.7)", {1.2, 0.75}, 0.7},
      {"no nickname expansion", {1.2, 0.75}, std::nullopt},
      {"nicknames x0.5", {1.2, 0.75}, 0.5},
      {"nicknames x1.0", {1.2, 0.75}, 1.0},
      {"b=0.5 (less length norm.)", {1.2, 0.5}, 0.7},
      {"b=0.9 (more length norm.)", {1.2, 0.9}, 0.7},
  };

  std::vector<Scores> means(configs.size());
  std::vector<std::vector<Scores>> per_query(configs.size());
  for (std::size_t c = 0; c < configs.size(); ++c) {
    const Config& cfg = configs[c];
    if (cfg.expansion.has_value()) {
      expander.set_weight(*cfg.expansion);
    }
    const gaffer::query::Searcher searcher(index, cfg.bm25,
                                           cfg.expansion.has_value() ? &expander : nullptr);
    for (const auto& q : queries) {
      const auto rel = qrels.find(q.id);
      if (rel == qrels.end()) {
        throw gaffer::Error("no judgements for " + q.id);
      }
      std::vector<std::string> ranked;
      for (const auto& hit : searcher.search(q.text, kDepth).hits) {
        ranked.push_back(corpus[hit.doc].key);
      }
      const Scores s{gaffer::eval::precision_at(ranked, rel->second, 5),
                     gaffer::eval::precision_at(ranked, rel->second, 10),
                     gaffer::eval::recall_at(ranked, rel->second, 10),
                     gaffer::eval::reciprocal_rank(ranked, rel->second)};
      per_query[c].push_back(s);
      means[c].p5 += s.p5;
      means[c].p10 += s.p10;
      means[c].r10 += s.r10;
      means[c].rr += s.rr;
    }
    const auto n = static_cast<double>(queries.size());
    means[c] = {means[c].p5 / n, means[c].p10 / n, means[c].r10 / n, means[c].rr / n};
  }

  std::cout << std::format(
      "Relevance evaluation: {} queries, {} judged-relevant docs, {}\n\n", queries.size(),
      [&] {
        std::size_t total = 0;
        for (const auto& [id, rel] : qrels) {
          total += rel.size();
        }
        return total;
      }(),
      opt.corpus);
  std::cout << std::format("{:<36} {:>6} {:>6} {:>6} {:>6}\n", "configuration", "P@5", "P@10",
                           "R@10", "MRR");
  for (std::size_t c = 0; c < configs.size(); ++c) {
    std::cout << std::format("{:<36} {:6.3f} {:6.3f} {:6.3f} {:6.3f}\n", configs[c].name,
                             means[c].p5, means[c].p10, means[c].r10, means[c].rr);
  }
  std::cout << std::format("\nPer query, {}:\n{:<4} {:<36} {:>4} {:>6} {:>6} {:>6} {:>6}\n",
                           configs[0].name, "qid", "query", "rel", "P@5", "P@10", "R@10", "RR");
  for (std::size_t i = 0; i < queries.size(); ++i) {
    const auto& s = per_query[0][i];
    std::cout << std::format("{:<4} {:<36} {:>4} {:6.3f} {:6.3f} {:6.3f} {:6.3f}\n", queries[i].id,
                             queries[i].text, qrels.at(queries[i].id).size(), s.p5, s.p10, s.r10,
                             s.rr);
  }

  if (opt.json) {
    std::ofstream out(*opt.json);
    out << "{\n  \"queries\": " << queries.size() << ",\n  \"configs\": [";
    for (std::size_t c = 0; c < configs.size(); ++c) {
      out << std::format(
          "{}\n    {{\"name\": \"{}\", \"p5\": {:.4f}, \"p10\": {:.4f}, \"r10\": {:.4f}, "
          "\"mrr\": {:.4f}}}",
          c == 0 ? "" : ",", configs[c].name, means[c].p5, means[c].p10, means[c].r10, means[c].rr);
    }
    out << "\n  ],\n  \"per_query_default\": [";
    for (std::size_t i = 0; i < queries.size(); ++i) {
      const auto& s = per_query[0][i];
      out << std::format(
          "{}\n    {{\"qid\": \"{}\", \"query\": \"{}\", \"p5\": {:.4f}, \"p10\": {:.4f}, "
          "\"r10\": {:.4f}, \"rr\": {:.4f}}}",
          i == 0 ? "" : ",", queries[i].id, queries[i].text, s.p5, s.p10, s.r10, s.rr);
    }
    out << "\n  ]\n}\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(parse_args(std::span<char*>(argv, static_cast<std::size_t>(argc))));
  } catch (const std::exception& e) {
    std::cerr << "gaffer-eval: " << e.what() << "\n";
    return 1;
  }
}
