// gaffer: the Ask the Gaffer command-line tool.
//
//   gaffer index  [options]            build the index, print collection stats
//                 [--out PATH]         ...and save it as a compressed index file
//   gaffer search [options] "<query>"  one query, ranked results
//                 [--index PATH]       ...using a saved index instead of rebuilding
//   gaffer search [options]            interactive: one query per line on stdin
//
// Options:
//   --corpus PATH      corpus TSV (default: $GAFFER_CORPUS or data/corpus/pl-2025-26.tsv)
//   -k N               results to show (default 10)
//   --k1 X, --b X      BM25 parameters (default 1.2, 0.75)
//   --no-stem          disable the S-stemmer
//   --keep-stopwords   index stopwords too
//   --nicknames PATH   club nickname file (default data/nicknames.tsv, if present)
//   --no-expand        don't expand nicknames ("gunners" -> Arsenal)
//   --var              "VAR review": explain each result's score term by term
//
// Exit codes: 0 ok, 1 runtime error (bad corpus file...), 2 usage error.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>  // isatty

#include "common/error.hpp"
#include "corpus/corpus.hpp"
#include "index/index_file.hpp"
#include "index/inverted_index.hpp"
#include "query/nicknames.hpp"
#include "query/searcher.hpp"
#include "ranking/bm25.hpp"
#include "snippet/snippet.hpp"
#include "text/analyzer.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view kDefaultCorpus = "data/corpus/pl-2025-26.tsv";
constexpr std::string_view kDefaultNicknames = "data/nicknames.tsv";

struct Options {
  std::string command;
  std::string corpus_path;
  std::optional<std::string> index_out;  // gaffer index --out PATH
  std::optional<std::string> index_in;   // gaffer search --index PATH
  std::string nicknames_path{kDefaultNicknames};
  bool nicknames_explicit = false;  // a missing file is only an error if the user named it
  bool expand = true;
  bool var = false;  // --var: per-term score breakdown ("VAR review")
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
    "usage: gaffer index  [--corpus PATH] [--out INDEX] [--no-stem] [--keep-stopwords]\n"
    "       gaffer search [--corpus PATH] [--index INDEX] [-k N] [--k1 X] [--b X] [--no-stem]\n"
    "                     [--keep-stopwords] [--nicknames PATH] [--no-expand] [--var]\n"
    "                     [\"query\"]\n";

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
    } else if (a == "--out" && opt.command == "index") {
      opt.index_out = value();
    } else if (a == "--index" && opt.command == "search") {
      opt.index_in = value();
    } else if (a == "--nicknames" && opt.command == "search") {
      opt.nicknames_path = value();
      opt.nicknames_explicit = true;
    } else if (a == "--var" && opt.command == "search") {
      opt.var = true;
    } else if (a == "--no-expand" && opt.command == "search") {
      opt.expand = false;
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

// Highlight markers: bold yellow on a terminal, **markdown** when piped to a
// file or another program (readable in logs, and easy to grep in tests).
struct Markers {
  std::string_view open;
  std::string_view close;
};

Markers markers() {
  if (isatty(STDOUT_FILENO) != 0) {
    return {"\033[1;33m", "\033[0m"};
  }
  return {"**", "**"};
}

// The VAR review: every term's tf, idf, weight and BM25 contribution, summed.
void print_var_review(const gaffer::ranking::Explanation& ex) {
  std::cout << std::format("     VAR review (doc length {}, average {:.1f}):\n", ex.doc_length,
                           ex.avg_doc_length);
  for (const auto& c : ex.parts) {
    std::string label = c.is_phrase ? std::format("\"{}\"", c.label) : c.label;
    if (!c.expanded_from.empty()) {
      label += std::format(" (from {})", c.expanded_from);
    }
    if (c.tf == 0) {
      std::cout << std::format("       {:<34} not in this document\n", label);
      continue;
    }
    std::cout << std::format("       {:<34} tf {:>2}  idf {:5.2f}  x{:.1f}  -> {:7.3f}\n", label,
                             c.tf, c.idf, c.weight, c.score);
  }
  std::cout << std::format("       {:<34} {:>32} {:7.3f}\n", "", "total", ex.total);
}

void print_results(const gaffer::corpus::Corpus& corpus, const gaffer::text::Analyzer& analyzer,
                   const gaffer::query::Searcher& searcher, const gaffer::query::SearchResult& r,
                   std::string_view query, double elapsed_ms, bool var) {
  std::cout << std::format("\nAsk the Gaffer: \"{}\"\n", query);
  std::string terms;
  for (const auto& ph : r.phrases) {
    std::string words;
    for (const auto& w : ph.terms) {
      words += (words.empty() ? "" : " ") + w;
    }
    terms += std::format("{}\"{}\" (phrase, required)", terms.empty() ? "" : ", ", words);
  }
  for (const auto& t : r.terms) {
    if (t.expanded_from.empty()) {
      terms += std::format("{}{} (df {})", terms.empty() ? "" : ", ", t.term, t.doc_freq);
    } else {
      terms += std::format("{}{} (df {}, from \"{}\" x{:.1f})", terms.empty() ? "" : ", ", t.term,
                           t.doc_freq, t.expanded_from, t.weight);
    }
  }
  std::cout << std::format("Kick-off: {}\n\n", terms.empty() ? "no searchable terms" : terms);

  if (r.hits.empty()) {
    std::cout << std::format("Goalless: no documents matched. ({:.2f} ms)\n", elapsed_ms);
    return;
  }
  const auto highlight = gaffer::query::highlight_terms(r);
  const Markers mark = markers();
  for (std::size_t i = 0; i < r.hits.size(); ++i) {
    const auto& hit = r.hits[i];
    const auto& doc = corpus[hit.doc];
    std::cout << std::format("{:>3}. {:<52} {:>7.3f}  {}\n", i + 1, doc.title, hit.score,
                             gaffer::corpus::to_string(doc.type));
    const auto snip = gaffer::snippet::make_snippet(doc.body, analyzer, highlight);
    std::cout << std::format("     {}\n", gaffer::snippet::render(snip, mark.open, mark.close));
    if (var) {
      print_var_review(searcher.explain(r, hit.doc));
    }
  }
  std::cout << std::format("\nFull-time: {} of {} matching documents in {:.2f} ms\n", r.hits.size(),
                           r.matched, elapsed_ms);
}

int run(const Options& opt) {
  auto start = Clock::now();
  const auto corpus = gaffer::corpus::load_tsv_file(opt.corpus_path);
  const double load_ms = ms_since(start);

  // Either load a saved index (gaffer search --index) or build one from the corpus.
  start = Clock::now();
  gaffer::index::InvertedIndex index;
  if (opt.index_in) {
    auto loaded = gaffer::index::load_index_file(*opt.index_in);
    if (loaded.corpus_fingerprint != gaffer::corpus::fingerprint(corpus)) {
      throw gaffer::Error(std::format("index '{}' was built from a different corpus than '{}'",
                                      *opt.index_in, opt.corpus_path));
    }
    index = std::move(loaded.index);
  } else {
    index = gaffer::index::build_index(corpus, gaffer::text::Analyzer{opt.analyzer});
  }
  const double index_ms = ms_since(start);

  if (opt.command == "index") {
    const auto& s = index.stats();
    std::cout << std::format(
        "Squad assembled from {}\n"
        "  documents:      {}\n"
        "  terms:          {}\n"
        "  postings:       {}\n"
        "  positions:      {}\n"
        "  tokens:         {}\n"
        "  avg doc length: {:.1f} tokens\n"
        "  load: {:.1f} ms   index: {:.1f} ms\n",
        opt.corpus_path, s.doc_count, index.term_count(), index.posting_count(),
        index.position_count(), s.total_length, s.avg_doc_length(), load_ms, index_ms);
    if (opt.index_out) {
      start = Clock::now();
      const auto fs = gaffer::index::write_index_file(index, gaffer::corpus::fingerprint(corpus),
                                                      *opt.index_out);
      std::cout << std::format(
          "Saved to {} in {:.1f} ms\n"
          "  file size:      {} bytes\n"
          "  postings:       {} bytes compressed, {} bytes as fixed-width uint32 ({:.1f}%)\n",
          *opt.index_out, ms_since(start), fs.file_bytes, fs.postings_bytes,
          fs.fixed_width_postings_bytes,
          100.0 * static_cast<double>(fs.postings_bytes) /
              static_cast<double>(std::max<std::size_t>(fs.fixed_width_postings_bytes, 1)));
    }
    return 0;
  }

  gaffer::query::NicknameExpander expander;
  if (opt.expand && (opt.nicknames_explicit || std::filesystem::exists(opt.nicknames_path))) {
    expander = gaffer::query::NicknameExpander::load_tsv_file(opt.nicknames_path);
  }
  const gaffer::query::Searcher searcher(index, opt.bm25, opt.expand ? &expander : nullptr);
  const gaffer::text::Analyzer analyzer(index.analyzer_options());  // for snippets
  auto answer = [&](std::string_view q) {
    const auto t0 = Clock::now();
    const auto result = searcher.search(q, opt.k);
    const double elapsed = ms_since(t0);  // search time only, not printing or VAR
    print_results(corpus, analyzer, searcher, result, q, elapsed, opt.var);
  };

  if (opt.query) {
    answer(*opt.query);
    return 0;
  }
  std::cerr << std::format("{} documents {} in {:.1f} ms. Ask the Gaffer (Ctrl-D to quit).\n",
                           corpus.size(), opt.index_in ? "loaded" : "indexed", index_ms);
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
  } catch (const std::exception& e) {  // bad_alloc, format_error, ...: report, don't abort
    std::cerr << "gaffer: internal error: " << e.what() << "\n";
    return 1;
  }
}
