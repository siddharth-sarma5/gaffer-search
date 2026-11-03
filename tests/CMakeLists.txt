# gaffer_add_test(<name> <sources...>) builds one GoogleTest binary per module
# and registers each TEST() with CTest, so `ctest` reports them individually.
function(gaffer_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE gaffer_core gaffer_warnings GTest::gtest_main)
  gtest_discover_tests(${name}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    PROPERTIES LABELS unit)
endfunction()

gaffer_add_test(common_test common/error_test.cpp)
gaffer_add_test(corpus_test corpus/corpus_test.cpp)
gaffer_add_test(text_test text/tokenizer_test.cpp text/analyzer_test.cpp)
gaffer_add_test(index_test index/inverted_index_test.cpp index/posting_cursor_test.cpp)
gaffer_add_test(ranking_test ranking/bm25_test.cpp)
gaffer_add_test(query_test query/top_k_test.cpp query/searcher_test.cpp
  query/query_parser_test.cpp)
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
