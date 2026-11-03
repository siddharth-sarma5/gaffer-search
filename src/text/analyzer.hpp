#pragma once

#include <string_view>
#include <vector>

#include "text/tokenizer.hpp"

namespace gaffer::text {

struct AnalyzerOptions {
  bool remove_stopwords = true;
  bool stem = true;
};

// The text-analysis pipeline: tokenize -> drop stopwords -> S-stem.
//
// Documents and queries MUST go through the same Analyzer configuration. If the
// index stemmed "goals" to "goal" but the query didn't, nothing would match.
// That's why the options are stored alongside the index.
//
// Positions are kept from the tokenizer, so removed stopwords leave gaps:
// "late winner at Anfield" -> late@0 winner@1 anfield@3. Phrase matching (M2)
// compares positions, and the gap stops "winner anfield" from matching as a
// phrase when the original text had a word between them.
class Analyzer {
 public:
  explicit Analyzer(AnalyzerOptions options = {}) noexcept : options_(options) {}

  [[nodiscard]] std::vector<Token> analyze(std::string_view text) const;
  [[nodiscard]] const AnalyzerOptions& options() const noexcept { return options_; }

 private:
  AnalyzerOptions options_;
};

}  // namespace gaffer::text
