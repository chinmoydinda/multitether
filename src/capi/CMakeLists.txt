add_library(
  tk_capi OBJECT
  capi_support.cc
  core_foundation_support.cc
  environment.cc
  net_config.cc
  orphan_cleanup.cc
  session.cc)

target_link_libraries(tk_capi PUBLIC multitether_base tk_version tk_core)

target_link_libraries(tk_capi PUBLIC "-framework CoreFoundation"
                                     "-framework SystemConfiguration")

if(MULTITETHER_BUILD_SHARED_LIB)
  add_library(multitether_shared SHARED)
  target_link_libraries(multitether_shared PRIVATE tk_capi)

  set_target_properties(
    multitether_shared
    PROPERTIES OUTPUT_NAME multitether
               VERSION "${PROJECT_VERSION}"
               SOVERSION "${PROJECT_VERSION_MAJOR}"
               INSTALL_NAME_DIR "@rpath")

  target_link_options(multitether_shared PRIVATE
                      "-Wl,-exported_symbols_list,${PROJECT_SOURCE_DIR}/cmake/multitether_exports.txt")

  install(TARGETS multitether_shared LIBRARY DESTINATION lib)
endif()

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/multitether/capi"
        DESTINATION include/multitether
        FILES_MATCHING
        PATTERN "*.h")
