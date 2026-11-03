#include "text/stemmer.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace gaffer::text {

namespace {

constexpr std::size_t kMinStemLength = 4;

bool has_digit(std::string_view s) noexcept {
  return std::ranges::any_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::string s_stem(std::string_view term) {
  if (term.size() < kMinStemLength || has_digit(term)) {
    return std::string(term);
  }
  if (term.ends_with("ies") && !term.ends_with("eies") && !term.ends_with("aies")) {
    std::string out(term.substr(0, term.size() - 3));
    out.push_back('y');
    return out;
  }
  if (term.ends_with("es") && !term.ends_with("aes") && !term.ends_with("ees") &&
      !term.ends_with("oes")) {
    return std::string(term.substr(0, term.size() - 1));  // drop the 's', keep the 'e'
  }
  if (term.ends_with('s') && !term.ends_with("us") && !term.ends_with("ss")) {
    return std::string(term.substr(0, term.size() - 1));
  }
  return std::string(term);
}

}  // namespace gaffer::text
