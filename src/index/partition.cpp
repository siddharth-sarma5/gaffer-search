#include "index/partition.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "common/error.hpp"
#include "common/types.hpp"
#include "corpus/corpus.hpp"

namespace gaffer::index {

std::vector<corpus::Corpus> partition(const corpus::Corpus& corpus, std::uint32_t num_shards) {
  if (num_shards == 0) {
    throw Error("number of shards must be at least 1");
  }
  std::vector<std::vector<corpus::Document>> parts(num_shards);
  for (DocId g = 0; g < corpus.size(); ++g) {
    parts[shard_of(g, num_shards)].push_back(corpus[g]);
  }
  std::vector<corpus::Corpus> out;
  out.reserve(num_shards);
  for (auto& docs : parts) {
    out.emplace_back(std::move(docs));
  }
  return out;
}

}  // namespace gaffer::index
