#include "text/analyzer.hpp"

#include <string_view>
#include <utility>
#include <vector>

#include "text/stemmer.hpp"
#include "text/stopwords.hpp"
#include "text/tokenizer.hpp"

namespace gaffer::text {

std::vector<Token> Analyzer::analyze(std::string_view text) const {
  std::vector<Token> tokens = tokenize(text);
  std::vector<Token> out;
  out.reserve(tokens.size());
  for (Token& tok : tokens) {
    if (options_.remove_stopwords && is_stopword(tok.term)) {
      continue;
    }
    if (options_.stem) {
      tok.term = s_stem(tok.term);
    }
    out.push_back(std::move(tok));
  }
  return out;
}

}  // namespace gaffer::text
