add_library(multitether_warnings INTERFACE)

set(_tk_warnings
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wvla
)

target_compile_options(multitether_warnings INTERFACE ${_tk_warnings})

if(MULTITETHER_WARNINGS_AS_ERRORS)
  target_compile_options(multitether_warnings INTERFACE -Werror)
endif()
