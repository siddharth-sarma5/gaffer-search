#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gaffer::eval {

// Relevance judgements: query id -> keys of the relevant documents (binary relevance).
using Qrels = std::map<std::string, std::set<std::string>>;

struct EvalQuery {
  std::string id;
  std::string text;
  std::string criterion;  // what "relevant" means for this query, in words
};

// Reads eval/queries.tsv ("qid<TAB>query<TAB>criterion") and eval/qrels.tsv
// ("qid<TAB>doc key"); '#' lines are comments. Throws gaffer::ParseError.
[[nodiscard]] std::vector<EvalQuery> load_queries(const std::filesystem::path& path);
[[nodiscard]] Qrels load_qrels(const std::filesystem::path& path);

// Standard IR metrics over a ranked list of doc keys (best first).
//
//  precision@k  = |relevant in top k| / k. The denominator is always k, so a
//                 query with 3 relevant docs can reach at most 0.3 at k = 10.
//  recall@k     = |relevant in top k| / |relevant|
//  reciprocal rank = 1 / rank of the first relevant doc (0 if none in the list).
//                 Averaged over queries it's MRR: "how far down is the first good answer?"
[[nodiscard]] double precision_at(const std::vector<std::string>& ranked,
                                  const std::set<std::string>& relevant, std::size_t k);
[[nodiscard]] double recall_at(const std::vector<std::string>& ranked,
                               const std::set<std::string>& relevant, std::size_t k);
[[nodiscard]] double reciprocal_rank(const std::vector<std::string>& ranked,
                                     const std::set<std::string>& relevant);

}  // namespace gaffer::eval
