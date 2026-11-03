#pragma once

#include <string>
#include <string_view>

namespace gaffer::text {

// The "S" stemmer (Harman, 1991): conflates plurals and nothing else.
// Rules, first match wins:
//   1. "-ies" -> "-y"   unless "-eies" or "-aies"   (penalties -> penalty)
//   2. "-es"  -> "-e"   unless "-aes", "-ees", "-oes" (braces -> brace)
//   3. "-s"   -> ""     unless "-us" or "-ss"        (goals -> goal; bonus, cross kept)
// Terms shorter than 4 bytes or containing a digit are left alone.
//
// Why not Porter? Porter is far more aggressive. It strips "-ing", "-ed" and
// "-ation", which mangles player names ("Ings" -> "ing", "Reading" -> "read")
// and is much harder to explain line by line. Plural folding gets most of the
// recall benefit ("goals" matches "goal") for a fraction of the risk.
//
// Known weakness: rule 2 keeps the 'e', so "matches" -> "matche" and doesn't
// conflate with "match". It is the price of a rule simple enough to fit in a
// header comment.
//
// Known collision: "reds" (Liverpool's nickname) stems to "red", as in red card.
// Nickname expansion (M2) maps "reds" to Liverpool to compensate.
[[nodiscard]] std::string s_stem(std::string_view term);

}  // namespace gaffer::text
