#pragma once

#include <cstdint>
#include <vector>

#include "common/types.hpp"
#include "corpus/corpus.hpp"

namespace gaffer::index {

// Document partitioning for the sharded deployment (D-013, D-019).
//
// Round-robin by global DocId:   shard(g) = g mod N,   local(g) = g div N,
// so                              global(local, shard) = local * N + shard.
//
// Why round-robin:
//  * Balanced: shard sizes differ by at most one document.
//  * No lookup table: a shard maps its local ids back to global ids by itself.
//  * Order-preserving within a shard: local order is global order, so a
//    shard's tie-breaks (lower DocId wins) agree with the single-node
//    tie-breaks. The sharded == single-node equivalence test relies on this.
// The trade-off vs hash-by-key partitioning: adding a document anywhere but
// the end renumbers everything after it. Fine for immutable, rebuilt-in-full
// indexes; a mutable index would hash stable keys instead.
[[nodiscard]] constexpr std::uint32_t shard_of(DocId global, std::uint32_t num_shards) noexcept {
  return global % num_shards;
}
[[nodiscard]] constexpr DocId local_of(DocId global, std::uint32_t num_shards) noexcept {
  return global / num_shards;
}
[[nodiscard]] constexpr DocId global_of(DocId local, std::uint32_t shard,
                                        std::uint32_t num_shards) noexcept {
  return local * num_shards + shard;
}

// Splits `corpus` into `num_shards` corpora (num_shards >= 1). Throws gaffer::Error
// if num_shards is 0.
[[nodiscard]] std::vector<corpus::Corpus> partition(const corpus::Corpus& corpus,
                                                    std::uint32_t num_shards);

}  // namespace gaffer::index
