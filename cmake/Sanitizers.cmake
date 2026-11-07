#      cmake -B build-tsan -DMULTITETHER_ENABLE_TSAN=ON && ctest --test-dir build-tsan

add_library(multitether_sanitizers INTERFACE)

if(MULTITETHER_ENABLE_ASAN AND MULTITETHER_ENABLE_TSAN)
  message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled together.")
endif()

set(_tk_san_list "")

if(MULTITETHER_ENABLE_ASAN)
  list(APPEND _tk_san_list address)
endif()

if(MULTITETHER_ENABLE_TSAN)
  list(APPEND _tk_san_list thread)
endif()

if(MULTITETHER_ENABLE_UBSAN)
  list(APPEND _tk_san_list undefined)
endif()

if(_tk_san_list)
  list(JOIN _tk_san_list "," _tk_san_flags)
  set(MULTITETHER_SANITIZER_SUMMARY "${_tk_san_flags}")
  target_compile_options(multitether_sanitizers INTERFACE -fsanitize=${_tk_san_flags}
                                                        -fno-omit-frame-pointer -g)
  target_link_options(multitether_sanitizers INTERFACE -fsanitize=${_tk_san_flags})
  message(STATUS "Enabled sanitizers: ${_tk_san_flags}")
else()
  set(MULTITETHER_SANITIZER_SUMMARY "none")
endif()
