add_library(
  tk_rndis STATIC
  protocol.cc
  messages.cc
  state_machine.cc)

target_link_libraries(tk_rndis PUBLIC multitether_base tk_common)
