cmake_minimum_required(VERSION 3.24)

set(CMAKE_OSX_DEPLOYMENT_TARGET "27.0" CACHE STRING "Minimum macOS version")

project(
  MultiTether
  VERSION 0.2.6
  DESCRIPTION "Multi-device USB tethering for macOS: libusb + feth + BPF"
  LANGUAGES CXX)

if(NOT APPLE)
  message(FATAL_ERROR "MultiTether requires macOS (feth and Darwin BPF).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MULTITETHER_MIN_MACOS_VERSION "27.0")

if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS
                                   MULTITETHER_MIN_MACOS_VERSION)
  message(
    FATAL_ERROR
      "CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} is too low; "
      "minimum ${MULTITETHER_MIN_MACOS_VERSION} is required by libc++ std::to_chars.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(MULTITETHER_BUILD_TESTS "Build unit tests" OFF)
option(MULTITETHER_BUILD_SHARED_LIB "Build the app backend library" ON)
option(MULTITETHER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MULTITETHER_NATIVE_ARCH "Optimize for the local CPU (-mcpu=native)" OFF)
option(MULTITETHER_ENABLE_LTO "Enable link-time optimization" OFF)
option(MULTITETHER_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(MULTITETHER_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(MULTITETHER_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

include(CompilerWarnings)
include(Sanitizers)
include(Optimizations)

find_package(LibUSB REQUIRED)

add_subdirectory(third_party)
add_subdirectory(src)

if(MULTITETHER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Optional GUI packaging target: cmake --build build --target gui
if(MULTITETHER_BUILD_SHARED_LIB AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gui/Package.swift")
  add_custom_target(
    gui
    COMMAND "${CMAKE_COMMAND}" -E env "MULTITETHER_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/gui/Scripts/build-gui.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS multitether_shared
    COMMENT "Build MultiTether and its helper"
    USES_TERMINAL
    VERBATIM)
  set_property(TARGET gui APPEND PROPERTY
               ADDITIONAL_CLEAN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/dist")
endif()

