// gaffer: the Ask the Gaffer command-line tool.
//
//   gaffer index  [options]            build the index, print collection stats
//   gaffer search [options] "<query>"  one query, ranked results
//   gaffer search [options]            interactive: one query per line on stdin
//
// Options:
//   --corpus PATH      corpus TSV (default: $GAFFER_CORPUS or data/corpus/pl-2025-26.tsv)
//   -k N               results to show (default 10)
//   --k1 X, --b X      BM25 parameters (default 1.2, 0.75)
//   --no-stem          disable the S-stemmer
//   --keep-stopwords   index stopwords too
//
// Exit codes: 0 ok, 1 runtime error (bad corpus file...), 2 usage error.

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "index/inverted_index.hpp"
#include "query/searcher.hpp"
#include "ranking/bm25.hpp"
#include "text/analyzer.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view kDefaultCorpus = "data/corpus/pl-2025-26.tsv";
constexpr std::size_t kPreviewChars = 110;

struct Options {
  std::string command;
  std::string corpus_path;
  std::optional<std::string> query;
  std::size_t k = 10;
  gaffer::ranking::Bm25Params bm25;
  gaffer::text::AnalyzerOptions analyzer;
};

class UsageError : public gaffer::Error {
 public:
  using gaffer::Error::Error;
};

constexpr std::string_view kUsage =
    "usage: gaffer index  [--corpus PATH] [--no-stem] [--keep-stopwords]\n"
    "       gaffer search [--corpus PATH] [-k N] [--k1 X] [--b X] [--no-stem]\n"
    "                     [--keep-stopwords] [\"query\"]\n";

double parse_double(std::string_view flag, const std::string& value) {
  try {
    std::size_t used = 0;
    const double v = std::stod(value, &used);
    if (used != value.size()) {
      throw UsageError("");
    }
    return v;
  } catch (const std::exception&) {
    throw UsageError(std::format("{} expects a number, got '{}'", flag, value));
  }
}

std::size_t parse_count(std::string_view flag, const std::string& value) {
  try {
    std::size_t used = 0;
    const unsigned long v = std::stoul(value, &used);
    if (used != value.size() || value.starts_with('-') || v == 0) {
      throw UsageError("");
    }
    return v;
  } catch (const std::exception&) {
    throw UsageError(std::format("{} expects a positive integer, got '{}'", flag, value));
  }
}

Options parse_args(const std::vector<std::string>& args) {
  if (args.empty() || (args[0] != "index" && args[0] != "search")) {
    throw UsageError("expected a command: index or search");
  }
  Options opt;
  opt.command = args[0];
  const char* env_corpus = std::getenv("GAFFER_CORPUS");  // NOLINT(concurrency-mt-unsafe)
  opt.corpus_path = env_corpus != nullptr ? env_corpus : std::string(kDefaultCorpus);

  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto value = [&]() -> const std::string& {
      if (i + 1 >= args.size()) {
        throw UsageError(std::format("{} needs a value", a));
      }
      return args[++i];
    };
    if (a == "--corpus") {
      opt.corpus_path = value();
    } else if (a == "-k") {
      opt.k = parse_count(a, value());
    } else if (a == "--k1") {
      opt.bm25.k1 = parse_double(a, value());
    } else if (a == "--b") {
      opt.bm25.b = parse_double(a, value());
    } else if (a == "--no-stem") {
      opt.analyzer.stem = false;
    } else if (a == "--keep-stopwords") {
      opt.analyzer.remove_stopwords = false;
    } else if (a.starts_with("-")) {
      throw UsageError(std::format("unknown option '{}'", a));
    } else if (opt.command == "search" && !opt.query) {
      opt.query = a;
    } else {
      throw UsageError(std::format("unexpected argument '{}'", a));
    }
  }
  return opt;
}

double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// First ~110 chars of the body, cut at a space so we never split a UTF-8 sequence.
std::string preview(const std::string& body) {
  if (body.size() <= kPreviewChars) {
    return body;
  }
  const std::size_t cut = body.rfind(' ', kPreviewChars);
  return body.substr(0, cut == std::string::npos ? kPreviewChars : cut) + " ...";
}

void print_results(const gaffer::corpus::Corpus& corpus, const gaffer::query::SearchResult& r,
                   std::string_view query, double elapsed_ms) {
  std::cout << std::format("\nAsk the Gaffer: \"{}\"\n", query);
  std::string terms;
  for (const auto& t : r.terms) {
    terms += std::format("{}{} (df {})", terms.empty() ? "" : ", ", t.term, t.doc_freq);
  }
  std::cout << std::format("Kick-off: {}\n\n", terms.empty() ? "no searchable terms" : terms);

  if (r.hits.empty()) {
    std::cout << std::format("Goalless: no documents matched. ({:.2f} ms)\n", elapsed_ms);
    return;
  }
  for (std::size_t i = 0; i < r.hits.size(); ++i) {
    const auto& hit = r.hits[i];
    const auto& doc = corpus[hit.doc];
    std::cout << std::format("{:>3}. {:<52} {:>7.3f}  {}\n", i + 1, doc.title, hit.score,
                             gaffer::corpus::to_string(doc.type));
    std::cout << std::format("     {}\n", preview(doc.body));
  }
  std::cout << std::format("\nFull-time: {} of {} matching documents in {:.2f} ms\n", r.hits.size(),
                           r.matched, elapsed_ms);
}

int run(const Options& opt) {
  auto start = Clock::now();
  const auto corpus = gaffer::corpus::load_tsv_file(opt.corpus_path);
  const double load_ms = ms_since(start);

  start = Clock::now();
  const auto index = gaffer::index::build_index(corpus, gaffer::text::Analyzer{opt.analyzer});
  const double index_ms = ms_since(start);

  if (opt.command == "index") {
    const auto& s = index.stats();
    std::cout << std::format(
        "Squad assembled from {}\n"
        "  documents:      {}\n"
        "  terms:          {}\n"
        "  postings:       {}\n"
        "  tokens:         {}\n"
        "  avg doc length: {:.1f} tokens\n"
        "  load: {:.1f} ms   index: {:.1f} ms\n",
        opt.corpus_path, s.doc_count, index.term_count(), index.posting_count(), s.total_length,
        s.avg_doc_length(), load_ms, index_ms);
    return 0;
  }

  const gaffer::query::Searcher searcher(index, opt.bm25);
  auto answer = [&](std::string_view q) {
    const auto t0 = Clock::now();
    const auto result = searcher.search(q, opt.k);
    print_results(corpus, result, q, ms_since(t0));
  };

  if (opt.query) {
    answer(*opt.query);
    return 0;
  }
  std::cerr << std::format("{} documents indexed in {:.1f} ms. Ask the Gaffer (Ctrl-D to quit).\n",
                           corpus.size(), index_ms);
  std::string line;
  while (std::cerr << "\ngaffer> " && std::getline(std::cin, line)) {
    if (!line.empty()) {
      answer(line);
    }
  }
  std::cerr << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    // span instead of pointer arithmetic on argv; drop argv[0] (program name).
    const std::span<char*> raw(argv, static_cast<std::size_t>(argc));
    const std::vector<std::string> args(raw.begin() + (raw.empty() ? 0 : 1), raw.end());
    return run(parse_args(args));
  } catch (const UsageError& e) {
    std::cerr << "gaffer: " << e.what() << "\n" << kUsage;
    return 2;
  } catch (const gaffer::Error& e) {
    std::cerr << "gaffer: " << e.what() << "\n";
    return 1;
  }
}
