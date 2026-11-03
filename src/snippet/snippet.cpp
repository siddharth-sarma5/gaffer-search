#include "snippet/snippet.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "text/analyzer.hpp"
#include "text/tokenizer.hpp"

namespace gaffer::snippet {

namespace {

struct WindowScore {
  std::size_t distinct = 0;
  std::size_t matches = 0;

  [[nodiscard]] bool better_than(const WindowScore& o) const noexcept {
    return std::pair(distinct, matches) > std::pair(o.distinct, o.matches);
  }
};

}  // namespace

Snippet make_snippet(std::string_view text, const text::Analyzer& analyzer,
                     const std::vector<std::string>& query_terms, SnippetOptions options) {
  const std::vector<text::Token> tokens = analyzer.analyze(text);
  Snippet out;
  if (tokens.empty()) {
    out.text = std::string(text);
    return out;
  }

  // For each token, the index of the query term it matches, or -1.
  std::vector<int> match(tokens.size(), -1);
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto it = std::ranges::find(query_terms, tokens[i].term);
    if (it != query_terms.end()) {
      match[i] = static_cast<int>(it - query_terms.begin());
    }
  }

  // Candidate windows start lead_in positions before each matching token.
  // Tokens are few per document (~100) and windows short, so the
  // O(matches * window) scan is simpler than a two-pointer sweep at no real cost.
  std::size_t best_first = 0;
  WindowScore best;
  std::vector<bool> seen(query_terms.size());
  for (std::size_t anchor = 0; anchor < tokens.size(); ++anchor) {
    if (match[anchor] < 0) {
      continue;
    }
    const std::uint32_t start_pos =
        tokens[anchor].position - std::min(tokens[anchor].position, options.lead_in);
    std::size_t first = anchor;
    while (first > 0 && tokens[first - 1].position >= start_pos) {
      --first;
    }
    WindowScore score;
    seen.assign(query_terms.size(), false);
    for (std::size_t i = first;
         i < tokens.size() && tokens[i].position < start_pos + options.window; ++i) {
      if (match[i] >= 0) {
        ++score.matches;
        if (!seen[static_cast<std::size_t>(match[i])]) {
          seen[static_cast<std::size_t>(match[i])] = true;
          ++score.distinct;
        }
      }
    }
    if (score.better_than(best)) {
      best = score;
      best_first = first;
    }
  }

  // Window bounds in tokens, then in bytes.
  std::size_t last = best_first;
  const std::uint32_t end_pos = tokens[best_first].position + options.window;
  while (last + 1 < tokens.size() && tokens[last + 1].position < end_pos) {
    ++last;
  }
  const std::size_t byte_begin = best_first == 0 ? 0 : tokens[best_first].begin;
  const std::size_t byte_end = last + 1 == tokens.size() ? text.size() : tokens[last].end;

  out.text = std::string(text.substr(byte_begin, byte_end - byte_begin));
  out.cut_start = byte_begin > 0;
  out.cut_end = byte_end < text.size();
  for (std::size_t i = best_first; i <= last; ++i) {
    if (match[i] >= 0) {
      out.highlights.push_back(Highlight{tokens[i].begin - byte_begin, tokens[i].end - byte_begin});
    }
  }
  return out;
}

std::string render(const Snippet& snippet, std::string_view open, std::string_view close) {
  std::string out;
  if (snippet.cut_start) {
    out += "... ";
  }
  std::size_t at = 0;
  for (const Highlight& h : snippet.highlights) {
    out.append(snippet.text, at, h.begin - at);
    out.append(open).append(snippet.text, h.begin, h.end - h.begin).append(close);
    at = h.end;
  }
  out.append(snippet.text, at);
  if (snippet.cut_end) {
    out += " ...";
  }
  return out;
}

}  // namespace gaffer::snippet
