#pragma once

#include <cstdint>

namespace gaffer::ranking {

// BM25 free parameters.
//   k1: tf saturation. 0 = tf ignored (binary), large = close to raw tf.
//       With k1 = 1.2, the 2nd occurrence of a term adds a lot and the 10th
//       adds little: repeating "Salah" can't dominate the score.
//   b:  length normalisation. 0 = none, 1 = full. Our corpus mixes 32-word
//       player docs with 206-word match docs, so b matters here.
// Defaults are the textbook values (Robertson et al.; also Lucene's defaults).
struct Bm25Params {
  double k1 = 1.2;
  double b = 0.75;
};

// Okapi BM25 for a single query term in a single document:
//
//   score = idf(df) * tf * (k1 + 1) / (tf + k1 * (1 - b + b * dl / avgdl))
//   idf   = ln(1 + (N - df + 0.5) / (df + 0.5))
//
// The "1 +" inside the log is Lucene's variant. The original Robertson-Sparck
// Jones idf goes negative for terms in more than half the documents, which
// would make matching a common term *lower* a document's score.
//
// Bm25 holds only N and avgdl and never touches an index. In M3 the broker
// builds one from *global* statistics, so every shard scores identically.
class Bm25 {
 public:
  // Throws gaffer::Error if k1 < 0 or b is outside [0, 1].
  Bm25(Bm25Params params, std::uint32_t doc_count, double avg_doc_length);

  // Inverse document frequency. Always > 0. Rarer terms score higher.
  [[nodiscard]] double idf(std::uint32_t doc_freq) const noexcept;

  // Contribution of one term to one document's score. `idf` is passed in
  // (rather than df) so the evaluator computes it once per query term, not
  // once per posting.
  [[nodiscard]] double score(double idf, std::uint32_t tf, std::uint32_t doc_length) const noexcept;

  [[nodiscard]] const Bm25Params& params() const noexcept { return params_; }

 private:
  Bm25Params params_;
  double doc_count_;
  double avg_doc_length_;
};

}  // namespace gaffer::ranking
