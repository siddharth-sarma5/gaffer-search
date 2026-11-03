#include "index/index_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "common/crc32.hpp"
#include "common/error.hpp"
#include "common/types.hpp"
#include "index/inverted_index.hpp"
#include "index/postings_codec.hpp"
#include "index/varint.hpp"
#include "text/analyzer.hpp"

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

std::uint32_t get_u32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
  std::uint32_t v = 0;
  for (unsigned i = 0; i < 4; ++i) {
    v |= static_cast<std::uint32_t>(b[at + i]) << (8U * i);
  }
  return v;
}

std::uint64_t get_u64(std::span<const std::uint8_t> b, std::size_t at) noexcept {
  std::uint64_t v = 0;
  for (unsigned i = 0; i < 8; ++i) {
    v |= static_cast<std::uint64_t>(b[at + i]) << (8U * i);
  }
  return v;
}

[[noreturn]] void corrupt(const std::string& what) {
  throw FormatError("corrupt index file: " + what);
}

// The value of a successful read, or a FormatError naming what was being read.
template <typename T>
T require(std::optional<T> value, const std::string& what) {
  if (!value) {
    corrupt(what);
  }
  return *value;
}

}  // namespace

// Befriended by InvertedIndex so the loader can fill its private members.
class IndexFileAccess {
 public:
  static InvertedIndex parse_body(VarintReader& in, std::size_t body_size,
                                  text::AnalyzerOptions options);
};

InvertedIndex IndexFileAccess::parse_body(VarintReader& in, std::size_t body_size,
                                          text::AnalyzerOptions options) {
  InvertedIndex idx;
  idx.analyzer_options_ = options;

  // Counts are bounded by the bytes available before anything is allocated,
  // so a corrupt count can't trigger a huge allocation.
  const std::uint32_t doc_count = require(in.read_u32(), "bad document count");
  if (doc_count > body_size) {
    corrupt("bad document count");
  }
  const std::uint64_t total_length = require(in.read_u64(), "bad total length");
  idx.doc_lengths_.reserve(doc_count);
  std::uint64_t sum = 0;
  for (std::uint32_t d = 0; d < doc_count; ++d) {
    const std::uint32_t len =
        require(in.read_u32(), "bad length for document " + std::to_string(d));
    idx.doc_lengths_.push_back(len);
    sum += len;
  }
  if (sum != total_length) {
    corrupt("document lengths don't add up to the stored total");
  }
  idx.stats_ = CollectionStats{doc_count, total_length};

  const std::uint32_t term_count = require(in.read_u32(), "bad term count");
  if (term_count > body_size) {
    corrupt("bad term count");
  }
  idx.postings_.reserve(term_count);
  const std::span<const std::uint8_t> body = in.data();
  std::string previous;
  for (std::uint32_t t = 0; t < term_count; ++t) {
    const std::uint32_t len =
        require(in.read_u32(), "bad term length for term " + std::to_string(t));
    if (len == 0 || len > in.remaining()) {
      corrupt("bad term length for term " + std::to_string(t));
    }
    const auto term_bytes = body.subspan(in.position(), len);
    std::string term(term_bytes.begin(), term_bytes.end());
    in.skip(len);
    if (t > 0 && term <= previous) {
      corrupt("terms are not strictly sorted at '" + term + "'");
    }
    const std::uint32_t plen =
        require(in.read_u32(), "bad postings length for term '" + term + "'");
    if (plen > in.remaining()) {
      corrupt("bad postings length for term '" + term + "'");
    }
    auto list = decode_posting_list(body.subspan(in.position(), plen), doc_count);
    if (!list || list->empty()) {
      corrupt("undecodable postings for term '" + term + "'");
    }
    in.skip(plen);
    idx.posting_count_ += list->size();
    idx.position_count_ += list->position_count();
    previous = term;
    idx.postings_.emplace(std::move(term), std::move(*list));
  }
  if (!in.at_end()) {
    corrupt("trailing bytes after the last term");
  }
  // Every analysed token has exactly one position, so these must agree.
  if (idx.position_count_ != idx.stats_.total_length) {
    corrupt("position count doesn't match total document length");
  }
  return idx;
}

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

LoadedIndex deserialize_index(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kIndexHeaderBytes + kIndexFooterBytes) {
    throw FormatError("not a gaffer index file: too small");
  }
  if (!std::equal(kIndexMagic.begin(), kIndexMagic.end(), bytes.begin())) {
    throw FormatError("not a gaffer index file: bad magic");
  }
  if (const std::uint32_t version = get_u32(bytes, 8); version != kIndexFormatVersion) {
    throw FormatError("unsupported index format version " + std::to_string(version) +
                      " (this build reads " + std::to_string(kIndexFormatVersion) + ")");
  }
  // Checksum before trusting anything else in the body.
  const auto covered = bytes.first(bytes.size() - kIndexFooterBytes);
  if (crc32(covered) != get_u32(bytes, bytes.size() - kIndexFooterBytes)) {
    throw FormatError("corrupt index file: checksum mismatch");
  }
  const std::uint32_t flags = get_u32(bytes, 12);
  if ((flags & ~(kFlagStem | kFlagStopwords)) != 0) {
    throw FormatError("corrupt index file: unknown flags");
  }
  const text::AnalyzerOptions options{.remove_stopwords = (flags & kFlagStopwords) != 0,
                                      .stem = (flags & kFlagStem) != 0};

  const auto body = covered.subspan(kIndexHeaderBytes);
  VarintReader in(body);
  return LoadedIndex{IndexFileAccess::parse_body(in, body.size(), options), get_u64(bytes, 16)};
}

LoadedIndex load_index_file(const std::filesystem::path& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  std::ifstream in(path, std::ios::binary);
  if (ec || !in) {
    throw IoError("cannot open index file '" + path.string() + "'");
  }
  std::vector<std::uint8_t> bytes(size);
  in.read(reinterpret_cast<char*>(bytes.data()),  // NOLINT(*-reinterpret-cast)
          static_cast<std::streamsize>(bytes.size()));
  if (!in) {
    throw IoError("failed reading index file '" + path.string() + "'");
  }
  try {
    return deserialize_index(bytes);
  } catch (const FormatError& e) {
    throw FormatError(path.string() + ": " + e.what());
  }
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
