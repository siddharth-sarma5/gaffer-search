add_executable(gaffer gaffer/main.cpp)
target_link_libraries(gaffer PRIVATE gaffer_core gaffer_warnings)

add_executable(gaffer-bench gaffer-bench/main.cpp)
target_link_libraries(gaffer-bench PRIVATE gaffer_core gaffer_warnings)
