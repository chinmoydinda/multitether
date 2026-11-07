add_library(
  tk_net STATIC
  feth_device.cc
  bpf_link.cc)

target_link_libraries(tk_net PUBLIC multitether_base tk_common)
