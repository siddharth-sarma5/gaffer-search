#include "corpus/document.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace gaffer::corpus {

namespace {

using namespace std::string_view_literals;

constexpr std::array<std::pair<std::string_view, DocType>, 4> kTypeNames{{
    {"match"sv, DocType::kMatch},
    {"club"sv, DocType::kClub},
    {"player"sv, DocType::kPlayer},
    {"season"sv, DocType::kSeason},
}};

}  // namespace

std::optional<DocType> parse_doc_type(std::string_view text) noexcept {
  for (const auto& [name, type] : kTypeNames) {
    if (name == text) {
      return type;
    }
  }
  return std::nullopt;
}

std::string_view to_string(DocType type) noexcept {
  for (const auto& [name, t] : kTypeNames) {
    if (t == type) {
      return name;
    }
  }
  return "unknown"sv;
}

}  // namespace gaffer::corpus
