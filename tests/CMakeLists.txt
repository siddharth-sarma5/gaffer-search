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
gaffer_add_test(index_test index/inverted_index_test.cpp)
gaffer_add_test(ranking_test ranking/bm25_test.cpp)
