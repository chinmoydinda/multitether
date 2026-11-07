add_library(
  tk_common STATIC
  error.cc
  i18n.cc
  logging.cc
  scheduling.cc)

target_link_libraries(tk_common PUBLIC multitether_base)
target_link_libraries(tk_common PRIVATE LibUSB::LibUSB)
