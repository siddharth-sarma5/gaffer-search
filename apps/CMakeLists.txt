add_executable(gaffer gaffer/main.cpp)
target_link_libraries(gaffer PRIVATE gaffer_core gaffer_warnings)

add_executable(gaffer-bench gaffer-bench/main.cpp)
target_link_libraries(gaffer-bench PRIVATE gaffer_core gaffer_warnings)

add_executable(gaffer-eval gaffer-eval/main.cpp)
target_link_libraries(gaffer-eval PRIVATE gaffer_core gaffer_warnings)

add_executable(gaffer-shard gaffer-shard/main.cpp)
target_link_libraries(gaffer-shard PRIVATE gaffer_core gaffer_warnings)
