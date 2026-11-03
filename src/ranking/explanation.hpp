#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/types.hpp"

namespace gaffer::ranking {

// One line of a "VAR review": how much one query term (or phrase) added to one
// document's BM25 score.
struct Contribution {
  std::string label;  // the analysed term, or the phrase's words joined by spaces
  bool is_phrase = false;
  std::string expanded_from;  // nickname alias that produced this term, if any
  std::uint32_t tf = 0;       // occurrences in the doc (phrase: number of phrase matches)
  double idf = 0.0;           // term idf, or the summed idf of a phrase's words
  double weight = 1.0;        // query_tf x expansion weight
  double score = 0.0;         // weight x bm25(idf, tf, doc_length); 0 when tf == 0
};

// Breakdown of a document's score. Invariant: summing parts[i].score in order
// reproduces the score the Searcher ranked by, bit for bit (same order, same
// arithmetic). A test enforces this.
struct Explanation {
  DocId doc = 0;
  std::uint32_t doc_length = 0;
  double avg_doc_length = 0.0;
  std::vector<Contribution> parts;  // phrases first, then free terms, in query order
  double total = 0.0;
};

}  // namespace gaffer::ranking
