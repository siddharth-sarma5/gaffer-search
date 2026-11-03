#include "index/index_file.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <system_error>
#include <vector>

#include "common/crc32.hpp"
#include "common/error.hpp"
#include "common/types.hpp"
#include "index/inverted_index.hpp"
#include "index/postings_codec.hpp"
#include "index/varint.hpp"

namespace gaffer::index {

namespace {

// Explicit little-endian byte order, regardless of the host CPU, so a file
// written on one machine reads on another.
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(v >> shift));
  }
}

void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(v >> shift));
  }
}

}  // namespace

std::vector<std::uint8_t> serialize_index(const InvertedIndex& index,
                                          std::uint64_t corpus_fingerprint, IndexFileStats* stats) {
  IndexFileStats s;
  std::vector<std::uint8_t> out;
  out.insert(out.end(), kIndexMagic.begin(), kIndexMagic.end());
  put_u32(out, kIndexFormatVersion);
  std::uint32_t flags = 0;
  if (index.analyzer_options().stem) {
    flags |= kFlagStem;
  }
  if (index.analyzer_options().remove_stopwords) {
    flags |= kFlagStopwords;
  }
  put_u32(out, flags);
  put_u64(out, corpus_fingerprint);

  const CollectionStats& cs = index.stats();
  put_varint(out, cs.doc_count);
  put_varint(out, cs.total_length);
  for (DocId d = 0; d < cs.doc_count; ++d) {
    put_varint(out, index.doc_length(d));
  }

  const auto terms = index.sorted_terms();
  put_varint(out, terms.size());
  for (const auto term : terms) {
    put_varint(out, term.size());
    out.insert(out.end(), term.begin(), term.end());
    const PostingList& list = index.posting_list(term);
    const std::vector<std::uint8_t> postings = encode_posting_list(list);
    put_varint(out, postings.size());
    out.insert(out.end(), postings.begin(), postings.end());
    s.postings_bytes += postings.size();
    s.fixed_width_postings_bytes += fixed_width_size(list);
  }

  put_u32(out, crc32(out));
  s.file_bytes = out.size();
  if (stats != nullptr) {
    *stats = s;
  }
  return out;
}

IndexFileStats write_index_file(const InvertedIndex& index, std::uint64_t corpus_fingerprint,
                                const std::filesystem::path& path) {
  IndexFileStats stats;
  const std::vector<std::uint8_t> bytes = serialize_index(index, corpus_fingerprint, &stats);

  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw IoError("cannot create index file '" + tmp.string() + "'");
    }
    // reinterpret_cast from uint8_t* to char* is the sanctioned way to hand
    // raw bytes to iostreams (char may alias any object).
    out.write(reinterpret_cast<const char*>(bytes.data()),  // NOLINT(*-reinterpret-cast)
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
      throw IoError("failed writing index file '" + tmp.string() + "'");
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);  // atomic replace on POSIX
  if (ec) {
    std::filesystem::remove(tmp, ec);
    throw IoError("cannot move index file into place at '" + path.string() + "'");
  }
  return stats;
}

}  // namespace gaffer::index
