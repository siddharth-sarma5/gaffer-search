# gaffer_core: the engine as a static library. Apps and tests link it; each
# module (corpus/, text/, index/, ...) adds its sources here as it lands.
add_library(gaffer_core STATIC
  common/crc32.cpp
  common/error.cpp
  corpus/corpus.cpp
  corpus/document.cpp
  eval/metrics.cpp
  index/index_file.cpp
  index/inverted_index.cpp
  index/partition.cpp
  index/posting_cursor.cpp
  index/postings_codec.cpp
  index/varint.cpp
  net/framing.cpp
  net/messages.cpp
  net/rpc.cpp
  net/socket.cpp
  query/nicknames.cpp
  query/phrase.cpp
  query/query_parser.cpp
  query/searcher.cpp
  query/top_k.cpp
  ranking/bm25.cpp
  server/broker.cpp
  server/global_stats.cpp
  server/shard_server.cpp
  server/tcp_server.cpp
  snippet/snippet.cpp
  text/analyzer.cpp
  text/ascii_fold.cpp
  text/stemmer.cpp
  text/stopwords.cpp
  text/tokenizer.cpp
)

# Headers are included relative to src/, e.g. #include "text/tokenizer.hpp".
target_include_directories(gaffer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(gaffer_core PUBLIC cxx_std_20)
target_link_libraries(gaffer_core PRIVATE gaffer_warnings)

# Threads are used by the M3 servers (std::jthread). Threads::Threads adds -pthread.
find_package(Threads REQUIRED)
target_link_libraries(gaffer_core PUBLIC Threads::Threads)
