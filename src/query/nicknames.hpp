#pragma once

#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gaffer::query {

// Weight of an expansion term relative to a word the user actually typed.
// Below 1 because an expansion is a guess about intent: "gunners" probably
// means Arsenal, but the literal word is the stronger evidence. Starting value;
// the relevance evaluation (M2-14) is where it gets checked.
inline constexpr double kExpansionWeight = 0.7;

struct Expansion {
  std::string alias;      // as listed, e.g. "red devils"
  std::string expansion;  // e.g. "Manchester United" (raw text; the Searcher analyses it)

  friend bool operator==(const Expansion&, const Expansion&) = default;
};

// Club nickname / short-name query expansion ("Gunners" -> Arsenal).
//
// Matching works on the tokenizer's output: lower-cased and accent-folded, but
// NOT stemmed or stopword-filtered. That matters: the S-stemmer maps "reds" to
// "red", so matching on stemmed terms would expand "red card" to Liverpool.
// Matching on raw tokens keeps "reds" and "red" apart.
// Multi-word aliases ("red devils", "man utd") match as consecutive tokens,
// longest alias first, and the tokens they consume aren't matched again.
class NicknameExpander {
 public:
  NicknameExpander() = default;
  explicit NicknameExpander(const std::vector<std::pair<std::string, std::string>>& entries);

  // Parses "alias<TAB>expansion" lines; '#' comments and blank lines are
  // skipped. Throws gaffer::ParseError on malformed lines or duplicate aliases.
  [[nodiscard]] static NicknameExpander load_tsv(std::istream& in, std::string_view source);
  [[nodiscard]] static NicknameExpander load_tsv_file(const std::filesystem::path& path);

  // Every alias found in `text`, in order of appearance.
  [[nodiscard]] std::vector<Expansion> find(std::string_view text) const;

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  // Weight given to expansion terms (default kExpansionWeight). Adjustable so
  // the relevance evaluation can compare settings.
  [[nodiscard]] double weight() const noexcept { return weight_; }
  void set_weight(double w) noexcept { weight_ = w; }

 private:
  struct Entry {
    std::vector<std::string> tokens;  // tokenized alias
    Expansion expansion;
  };
  std::vector<Entry> entries_;  // sorted by token count, longest first
  double weight_ = kExpansionWeight;
};

}  // namespace gaffer::query
