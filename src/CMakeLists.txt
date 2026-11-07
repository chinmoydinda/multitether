add_library(multitether_base INTERFACE)
target_include_directories(multitether_base INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(multitether_base INTERFACE multitether_warnings multitether_sanitizers
                                               multitether_optimizations)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.cc.in" "${CMAKE_CURRENT_BINARY_DIR}/version.cc"
               @ONLY)

add_library(tk_version STATIC "${CMAKE_CURRENT_BINARY_DIR}/version.cc")
target_link_libraries(tk_version PUBLIC multitether_base)

add_subdirectory(common)
add_subdirectory(rndis)
add_subdirectory(net)
add_subdirectory(usb)
add_subdirectory(core)
add_subdirectory(capi)
