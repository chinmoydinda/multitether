#ifndef MULTITETHER_CAPI_MULTITETHER_C_H_
#define MULTITETHER_CAPI_MULTITETHER_C_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(_WIN32)
#define TK_API
#else
#define TK_API __attribute__((visibility("default")))
#endif

#define TK_ERROR_MESSAGE_CAPACITY 1024

#define TK_INTERFACE_NAME_CAPACITY 16

#define TK_USB_STRING_CAPACITY 128

#define TK_ADDRESS_CAPACITY 46

typedef enum tk_result {
  TK_OK = 0,
  TK_ERR_INVALID_ARGUMENT = -1,
  TK_ERR_INVALID_STATE = -2,
  TK_ERR_FAILED = -3,
  TK_ERR_PERMISSION = -4,
  TK_ERR_NOT_FOUND = -5,
} tk_result_t;

typedef enum tk_error_domain {
  TK_ERROR_DOMAIN_GENERIC = 0,
  TK_ERROR_DOMAIN_ERRNO = 1,   ///< POSIX errno.
  TK_ERROR_DOMAIN_LIBUSB = 2,
  TK_ERROR_DOMAIN_RNDIS = 3,   ///< RNDIS_STATUS_*.
} tk_error_domain_t;

typedef struct tk_error {
  int32_t domain;  ///< tk_error_domain_t
  int64_t code;
  char message[TK_ERROR_MESSAGE_CAPACITY];
} tk_error_t;

typedef struct tk_device_info {
  uint16_t vendor_id;
  uint16_t product_id;
  uint8_t bus_number;
  uint8_t device_address;

  char manufacturer[TK_USB_STRING_CAPACITY];
  char product[TK_USB_STRING_CAPACITY];
  char serial[TK_USB_STRING_CAPACITY];
} tk_device_info_t;

TK_API tk_result_t tk_list_devices(tk_device_info_t* out_devices, size_t capacity,
                                   size_t* out_count, bool read_strings, tk_error_t* out_error);

typedef enum tk_run_state {
  TK_RUN_IDLE = 0,
  TK_RUN_STARTING = 1,
  TK_RUN_RUNNING = 2,
  TK_RUN_STOPPING = 3,
  TK_RUN_STOPPED = 4,
  TK_RUN_FAILED = 5,
} tk_run_state_t;

typedef struct tk_session_config {
  uint16_t vendor_id;
  uint16_t product_id;

  uint8_t bus_number;
  uint8_t device_address;
} tk_session_config_t;

TK_API void tk_session_config_init(tk_session_config_t* out_config);

typedef struct tk_session_status {
  int32_t run_state;  ///< tk_run_state_t

  char system_interface[TK_INTERFACE_NAME_CAPACITY];

  char driver_interface[TK_INTERFACE_NAME_CAPACITY];

  char device_description[64];

  uint64_t rx_bytes;
  uint64_t tx_bytes;

  int64_t monotonic_nanos;

  char fatal[TK_ERROR_MESSAGE_CAPACITY];
} tk_session_status_t;

typedef struct tk_session tk_session_t;

TK_API tk_session_t* tk_session_create(const tk_session_config_t* config, tk_error_t* out_error);

TK_API tk_result_t tk_session_start(tk_session_t* session, tk_error_t* out_error);

TK_API tk_result_t tk_session_stop(tk_session_t* session);

TK_API void tk_session_destroy(tk_session_t* session);

TK_API tk_result_t tk_session_status_get(tk_session_t* session, tk_session_status_t* out_status);

typedef struct tk_net_state {
  bool has_address;
  char address[TK_ADDRESS_CAPACITY];
  char netmask[TK_ADDRESS_CAPACITY];
  char router[TK_ADDRESS_CAPACITY];

  bool has_default_route;
} tk_net_state_t;

TK_API tk_result_t tk_net_query(const char* interface_name, tk_net_state_t* out_state,
                                tk_error_t* out_error);

TK_API tk_result_t tk_cleanup_orphan_interfaces(size_t* out_removed, tk_error_t* out_error);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // MULTITETHER_CAPI_MULTITETHER_C_H_
