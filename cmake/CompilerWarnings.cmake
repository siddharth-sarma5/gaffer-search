# gaffer_warnings: an INTERFACE target carrying our warning flags.
# Our own targets link it; third-party code (GoogleTest) does not, so we never
# have to fix warnings in code we don't own.
add_library(gaffer_warnings INTERFACE)

if(MSVC)
  target_compile_options(gaffer_warnings INTERFACE /W4 /permissive-)
  if(GAFFER_WARNINGS_AS_ERRORS)
    target_compile_options(gaffer_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(gaffer_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow                # a local hides an outer name: a classic source of bugs
    -Wconversion            # implicit narrowing, e.g. size_t -> uint32_t doc ids
    -Wsign-conversion
    -Wold-style-cast        # force static_cast & co, which are greppable and checked
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion      # float silently widened to double in scoring code
    -Wimplicit-fallthrough
  )
  if(GAFFER_WARNINGS_AS_ERRORS)
    target_compile_options(gaffer_warnings INTERFACE -Werror)
  endif()
endif()
