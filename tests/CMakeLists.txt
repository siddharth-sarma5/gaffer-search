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
