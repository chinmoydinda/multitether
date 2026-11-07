find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
  pkg_check_modules(PC_LIBUSB QUIET libusb-1.0)
endif()

find_path(
  LibUSB_INCLUDE_DIR
  NAMES libusb.h
  HINTS ${PC_LIBUSB_INCLUDEDIR} ${PC_LIBUSB_INCLUDE_DIRS}
  PATHS /opt/homebrew/opt/libusb/include /usr/local/opt/libusb/include /opt/local/include
        /usr/local/include /usr/include
  PATH_SUFFIXES libusb-1.0)

find_library(
  LibUSB_LIBRARY
  NAMES usb-1.0 libusb-1.0
  HINTS ${PC_LIBUSB_LIBDIR} ${PC_LIBUSB_LIBRARY_DIRS}
  PATHS /opt/homebrew/opt/libusb/lib /usr/local/opt/libusb/lib /opt/local/lib /usr/local/lib
        /usr/lib)

if(PC_LIBUSB_VERSION)
  set(LibUSB_VERSION "${PC_LIBUSB_VERSION}")
elseif(LibUSB_INCLUDE_DIR AND EXISTS "${LibUSB_INCLUDE_DIR}/libusb.h")
  file(STRINGS "${LibUSB_INCLUDE_DIR}/libusb.h" _libusb_api_line
       REGEX "^#define[ \t]+LIBUSB_API_VERSION[ \t]+0x[0-9A-Fa-f]+")
  if(_libusb_api_line)
    string(REGEX REPLACE ".*0x([0-9A-Fa-f]+).*" "\\1" LibUSB_VERSION "${_libusb_api_line}")
    set(LibUSB_VERSION "API-0x${LibUSB_VERSION}")
  else()
    set(LibUSB_VERSION "unknown")
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  LibUSB
  REQUIRED_VARS LibUSB_LIBRARY LibUSB_INCLUDE_DIR
  VERSION_VAR LibUSB_VERSION)

if(LibUSB_FOUND AND NOT TARGET LibUSB::LibUSB)
  add_library(LibUSB::LibUSB UNKNOWN IMPORTED)
  set_target_properties(
    LibUSB::LibUSB
    PROPERTIES IMPORTED_LOCATION "${LibUSB_LIBRARY}"
               INTERFACE_INCLUDE_DIRECTORIES "${LibUSB_INCLUDE_DIR}")
  find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  find_library(SECURITY_FRAMEWORK Security REQUIRED)
  set_property(
    TARGET LibUSB::LibUSB
    APPEND
    PROPERTY INTERFACE_LINK_LIBRARIES "${IOKIT_FRAMEWORK}" "${COREFOUNDATION_FRAMEWORK}"
             "${SECURITY_FRAMEWORK}")
endif()

mark_as_advanced(LibUSB_INCLUDE_DIR LibUSB_LIBRARY)
