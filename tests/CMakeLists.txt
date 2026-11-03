# gaffer_add_test(<name> <sources...>) builds one GoogleTest binary per module
# and registers each TEST() with CTest, so `ctest` reports them individually.
function(gaffer_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE gaffer_core gaffer_warnings GTest::gtest_main)
  gtest_discover_tests(${name}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    PROPERTIES LABELS unit)
endfunction()

gaffer_add_test(common_test common/error_test.cpp common/crc32_test.cpp)
gaffer_add_test(corpus_test corpus/corpus_test.cpp)
gaffer_add_test(text_test text/tokenizer_test.cpp text/analyzer_test.cpp)
gaffer_add_test(index_test index/inverted_index_test.cpp index/posting_cursor_test.cpp
  index/varint_test.cpp index/postings_codec_test.cpp index/index_file_test.cpp
  index/partition_test.cpp)
gaffer_add_test(ranking_test ranking/bm25_test.cpp)
gaffer_add_test(query_test query/top_k_test.cpp query/searcher_test.cpp
  query/query_parser_test.cpp query/phrase_test.cpp query/nicknames_test.cpp)
gaffer_add_test(e2e_test e2e/e2e_test.cpp)

# CLI smoke tests: run the real binary and check its output and exit codes.
add_test(NAME cli.search
  COMMAND gaffer search "late winner at anfield" -k 3
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.search PROPERTIES LABELS cli
  PASS_REGULAR_EXPRESSION "Full-time: 3 of [0-9]+ matching documents")

add_test(NAME cli.index
  COMMAND gaffer index
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.index PROPERTIES LABELS cli
  PASS_REGULAR_EXPRESSION "documents: +938")

add_test(NAME cli.no_match
  COMMAND gaffer search "the of at"
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.no_match PROPERTIES LABELS cli PASS_REGULAR_EXPRESSION "Goalless")

# Usage and runtime errors must exit non-zero.
add_test(NAME cli.usage_error COMMAND gaffer search -k 0 x)
set_tests_properties(cli.usage_error PROPERTIES LABELS cli WILL_FAIL TRUE)
add_test(NAME cli.missing_corpus COMMAND gaffer search --corpus does-not-exist.tsv x)
set_tests_properties(cli.missing_corpus PROPERTIES LABELS cli WILL_FAIL TRUE)

# Save an index, then search with it (FIXTURES orders the two tests).
set(GAFFER_TEST_INDEX ${CMAKE_CURRENT_BINARY_DIR}/pl-2025-26.gaffer)
add_test(NAME cli.index_save
  COMMAND gaffer index --out ${GAFFER_TEST_INDEX}
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.index_save PROPERTIES LABELS cli FIXTURES_SETUP saved_index
  PASS_REGULAR_EXPRESSION "Saved to .*file size: +[0-9]+ bytes")
add_test(NAME cli.search_saved_index
  COMMAND gaffer search --index ${GAFFER_TEST_INDEX} "\"north london derby\""
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.search_saved_index PROPERTIES LABELS cli FIXTURES_REQUIRED saved_index
  PASS_REGULAR_EXPRESSION "Full-time: 2 of 2 matching documents")
# The saved index must be refused with a different corpus.
add_test(NAME cli.index_corpus_mismatch
  COMMAND gaffer search --corpus tests/fixtures/mini_corpus.tsv --index ${GAFFER_TEST_INDEX} x
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.index_corpus_mismatch PROPERTIES LABELS cli FIXTURES_REQUIRED saved_index
  WILL_FAIL TRUE)
gaffer_add_test(snippet_test snippet/snippet_test.cpp)
add_test(NAME cli.snippet_highlight
  COMMAND gaffer search "late winner at anfield" -k 1
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.snippet_highlight PROPERTIES LABELS cli
  PASS_REGULAR_EXPRESSION "scored a \\*\\*late\\*\\* \\*\\*winner\\*\\*")
add_test(NAME cli.nickname_expansion
  COMMAND gaffer search "gunners late winner" -k 1
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.nickname_expansion PROPERTIES LABELS cli
  PASS_REGULAR_EXPRESSION "arsenal \\(df [0-9]+, from \"gunners\" x0.7\\)")
add_test(NAME cli.var_review
  COMMAND gaffer search "late winner at anfield" -k 1 --var
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.var_review PROPERTIES LABELS cli
  PASS_REGULAR_EXPRESSION "VAR review.*anfield +tf +[0-9]+ +idf.*total")
# Smoke test: the benchmark runs end to end (tiny settings; not a measurement).
add_test(NAME bench.smoke
  COMMAND gaffer-bench --runs 1 --reps 1
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(bench.smoke PROPERTIES LABELS bench PASS_REGULAR_EXPRESSION "latency: +p50")
gaffer_add_test(eval_test eval/metrics_test.cpp)
gaffer_add_test(net_test net/socket_test.cpp net/messages_test.cpp)
gaffer_add_test(server_test server/shard_server_test.cpp server/global_stats_test.cpp)
add_test(NAME eval.smoke COMMAND gaffer-eval WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(eval.smoke PROPERTIES LABELS eval PASS_REGULAR_EXPRESSION "default .* MRR|MRR")
add_test(NAME cli.partition
  COMMAND gaffer partition --shards 3 --out-dir ${CMAKE_CURRENT_BINARY_DIR}/shards
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(cli.partition PROPERTIES LABELS cli
  PASS_REGULAR_EXPRESSION "shard 2: 312 docs")
