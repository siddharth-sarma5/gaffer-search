#pragma once

#include <cstdint>

namespace gaffer {

// Dense document id: the position of a document in its corpus. 32 bits holds
// 4 billion docs, far more than we'll index, and halves postings memory compared
// with size_t. The loader checks the limit.
using DocId = std::uint32_t;

}  // namespace gaffer
