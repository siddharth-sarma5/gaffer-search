# gaffer_core: the engine as a static library. Apps and tests link it; each
# module (corpus/, text/, index/, ...) adds its sources here as it lands.
add_library(gaffer_core STATIC
  common/error.cpp
  corpus/corpus.cpp
  corpus/document.cpp
  text/ascii_fold.cpp
  text/tokenizer.cpp
)

# Headers are included relative to src/, e.g. #include "text/tokenizer.hpp".
target_include_directories(gaffer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(gaffer_core PUBLIC cxx_std_20)
target_link_libraries(gaffer_core PRIVATE gaffer_warnings)
