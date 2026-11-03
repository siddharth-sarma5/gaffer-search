# gaffer_enable_sanitizers("address,undefined") adds -fsanitize flags to every
# target defined after the call.
#
#   address    (ASan)  out-of-bounds, use-after-free, leaks
#   undefined  (UBSan) signed overflow, misaligned loads, bad shifts, ...
#   thread     (TSan)  data races (used for the concurrent M3 code)
#
# ASan and TSan instrument memory differently and cannot be combined.
function(gaffer_enable_sanitizers sanitizers)
  if(sanitizers STREQUAL "")
    return()
  endif()
  if(MSVC)
    message(FATAL_ERROR "GAFFER_SANITIZE is only supported with GCC/Clang")
  endif()
  if(sanitizers MATCHES "thread" AND sanitizers MATCHES "address")
    message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer")
  endif()

  message(STATUS "gaffer: enabling sanitizers: ${sanitizers}")
  set(flags
    -fsanitize=${sanitizers}
    -fno-omit-frame-pointer        # readable stack traces in reports
    -fno-sanitize-recover=all)     # make UBSan findings fail the test, not just log
  add_compile_options(${flags})
  add_link_options(-fsanitize=${sanitizers})
endfunction()
