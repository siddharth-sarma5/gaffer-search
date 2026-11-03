#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gaffer::corpus {

enum class DocType : std::uint8_t { kMatch, kClub, kPlayer, kSeason };

[[nodiscard]] std::optional<DocType> parse_doc_type(std::string_view text) noexcept;
[[nodiscard]] std::string_view to_string(DocType type) noexcept;

// One searchable document. `key` is the stable string id from the corpus file
// ("match-2025-08-15-liverpool-bournemouth"). The dense numeric DocId is simply
// the document's position in its Corpus, so it isn't stored here.
struct Document {
  std::string key;
  DocType type{DocType::kMatch};
  std::string title;
  std::string body;
};

}  // namespace gaffer::corpus
