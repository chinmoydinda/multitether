add_library(multitether_optimizations INTERFACE)

target_compile_options(
  multitether_optimizations
  INTERFACE
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -fno-strict-aliasing)

if(MULTITETHER_NATIVE_ARCH)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-mcpu=native TK_HAS_MCPU_NATIVE)
  if(TK_HAS_MCPU_NATIVE)
    target_compile_options(multitether_optimizations INTERFACE -mcpu=native)
  else()
    message(WARNING "Compiler does not support -mcpu=native; skipping native CPU optimization.")
  endif()
endif()

if(MULTITETHER_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT TK_IPO_OK OUTPUT TK_IPO_MSG)
  if(TK_IPO_OK)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON PARENT_SCOPE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON PARENT_SCOPE)
  else()
    message(WARNING "LTO unavailable: ${TK_IPO_MSG}")
  endif()
endif()
