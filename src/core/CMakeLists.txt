add_library(
  tk_core STATIC
  bridge.cc
  runtime.cc)

target_link_libraries(tk_core PUBLIC multitether_base tk_common tk_rndis tk_net tk_usb)
