add_executable(
  multitether_tests
  loopback_link.cc
  test_main.cc
  test_version.cc
  test_common_byte_order.cc
  test_common_error.cc
  test_common_frame_ring.cc
  test_common_i18n.cc
  test_common_spsc_ring.cc
  test_common_stats.cc
  test_rndis_messages.cc
  test_rndis_packet_codec.cc
  test_rndis_state_machine.cc
  test_net_link.cc
  test_core_bridge.cc
  test_capi.cc
  test_usb_device_filter.cc)

target_link_libraries(multitether_tests PRIVATE multitether_base doctest tk_capi)
target_include_directories(multitether_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                   "${PROJECT_SOURCE_DIR}/src/capi")

add_test(NAME all COMMAND multitether_tests)

set(MULTITETHER_TEST_SUITES
    version
    common.byte_order
    common.error
    common.frame_ring
    common.frame_ring_batch
    common.i18n
    common.spsc_ring
    common.stats
    rndis.messages
    rndis.packet_codec
    rndis.state_machine
    rndis.state_machine_timeout
    usb.device_filter
    net.abi
    net.loopback
    net.feth
    core.bridge
    capi.support
    capi.basics
    capi.session
    capi.net_config)

foreach(_suite IN LISTS MULTITETHER_TEST_SUITES)
  add_test(NAME "${_suite}" COMMAND multitether_tests --test-suite=${_suite})
  set_tests_properties("${_suite}" PROPERTIES FAIL_REGULAR_EXPRESSION "assertions: +0 \\|")
endforeach()
