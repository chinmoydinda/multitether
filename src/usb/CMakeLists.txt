add_library(
  tk_usb STATIC
  context.cc
  device.cc
  data_channel.cc)

target_link_libraries(tk_usb PUBLIC multitether_base tk_common tk_rndis)
target_link_libraries(tk_usb PUBLIC LibUSB::LibUSB)
