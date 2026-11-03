#include "ranking/bm25.hpp"

#include <cmath>
#include <cstdint>

#include "common/error.hpp"

namespace gaffer::ranking {

Bm25::Bm25(Bm25Params params, std::uint32_t doc_count, double avg_doc_length)
    : params_(params), doc_count_(static_cast<double>(doc_count)), avg_doc_length_(avg_doc_length) {
  // NaN compares false with everything, so check for it explicitly.
  if (std::isnan(params_.k1) || params_.k1 < 0.0) {
    throw Error("BM25 k1 must be >= 0");
  }
  if (std::isnan(params_.b) || params_.b < 0.0 || params_.b > 1.0) {
    throw Error("BM25 b must be in [0, 1]");
  }
}

double Bm25::idf(std::uint32_t doc_freq) const noexcept {
  const auto df = static_cast<double>(doc_freq);
  return std::log(1.0 + (doc_count_ - df + 0.5) / (df + 0.5));
}

double Bm25::score(double idf, std::uint32_t tf, std::uint32_t doc_length) const noexcept {
  const auto f = static_cast<double>(tf);
  // An empty collection has avgdl 0. Treat every document as average length
  // instead of dividing by zero.
  const double relative_length =
      avg_doc_length_ > 0.0 ? static_cast<double>(doc_length) / avg_doc_length_ : 1.0;
  const double norm = params_.k1 * (1.0 - params_.b + params_.b * relative_length);
  return idf * f * (params_.k1 + 1.0) / (f + norm);
}

}  // namespace gaffer::ranking
