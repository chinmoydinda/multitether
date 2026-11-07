#pragma once

#include <net/bpf.h>
#include <net/if.h>
#include <sys/ioccom.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <cstddef>
#include <cstdint>

#include "multitether/common/i18n.h"

namespace multitether::net {
// Darwin ifdrv ioctl ABI

struct IfDrv {
  char ifd_name[IFNAMSIZ];
  unsigned long ifd_cmd;
  std::size_t ifd_len;
  void* ifd_data;
};

static_assert(sizeof(IfDrv) == 40, "struct ifdrv must be 40 bytes for ioctl encoding");
static_assert(offsetof(IfDrv, ifd_cmd) == 16);
static_assert(offsetof(IfDrv, ifd_len) == 24);
static_assert(offsetof(IfDrv, ifd_data) == 32);

inline constexpr unsigned long kSetDriverSpec = _IOW('i', 123, IfDrv);
inline constexpr unsigned long kGetDriverSpec = _IOWR('i', 123, IfDrv);

static_assert(kSetDriverSpec == 0x8028'697BUL, "Unexpected SIOCSDRVSPEC encoding");
static_assert(kGetDriverSpec == 0xC028'697BUL, "Unexpected SIOCGDRVSPEC encoding");

// feth fake-ethernet driver specification

inline constexpr const char* kFethCloneName = "feth";

enum class FethSetCommand : unsigned long {
  kNone = 0,
  kSetPeer = 1,
  kSetMedia = 2,
  kSetDequeueStall = 3,
};

enum class FethGetCommand : unsigned long {
  kNone = 0,
  kGetPeer = 1,
};

inline constexpr std::size_t kFethMediaListMax = 27;

/// Mirrors XNU struct if_fake_request (bsd/net/if_fake_var.h).
struct FethRequest {
  std::uint64_t reserved[4];
  union {
    char buffer[128];
    struct {
      std::int32_t current;
      std::uint32_t count;
      std::uint32_t media_reserved[3];
      std::int32_t list[kFethMediaListMax];
    } media;
    char peer_name[IFNAMSIZ];
    std::uint32_t dequeue_stall;
  } u;
};

static_assert(sizeof(FethRequest) == 160, "struct if_fake_request must be 160 bytes");
static_assert(offsetof(FethRequest, u) == 32);
static_assert(sizeof(FethRequest::u) == 128);
static_assert(alignof(FethRequest) == 8);

// Darwin BPF ioctl extensions and header layout

inline constexpr unsigned long kBpfSetBatchWrite = _IOW('B', 143, int);
static_assert(kBpfSetBatchWrite == 0x8004'428FUL, "Unexpected BIOCSBATCHWRITE encoding");

inline constexpr unsigned long kBpfSetNoTimestamp = _IOW('B', 145, int);
static_assert(kBpfSetNoTimestamp == 0x8004'4291UL, "Unexpected BIOCSNOTSTAMP encoding");

inline constexpr unsigned long kBpfSetDirection = _IOW('B', 138, unsigned int);

inline constexpr unsigned int kBpfDirectionNone = 0;
inline constexpr unsigned int kBpfDirectionIn = 0x1;
inline constexpr unsigned int kBpfDirectionOut = 0x2;
inline constexpr unsigned int kBpfDirectionInOut = kBpfDirectionIn | kBpfDirectionOut;

/// Minimum BPF header length on Darwin (SIZEOF_BPF_HDR = 18).
inline constexpr std::uint32_t kBpfHeaderMinBytes = 18;

static_assert(sizeof(struct bpf_hdr) == 20,
              "LP64 bpf_hdr must be 20 bytes (18 bytes plus padding)");
static_assert(sizeof(struct BPF_TIMEVAL) == 8,
              "LP64 BPF timestamps use the 8-byte timeval32 layout");

// Required feth sysctl settings

struct RequiredFethSysctl {
  const char* name;
  std::int32_t required_value;
  Msg why;
};

inline constexpr RequiredFethSysctl kRequiredFethSysctls[] = {
    {"net.link.fake.hwcsum", 0, Msg::kNetSysctlWhyHwcsum},
    {"net.link.fake.fcs", 0, Msg::kNetSysctlWhyFcs},
    {"net.link.fake.tso_support", 0, Msg::kNetSysctlWhyTso},
    {"net.link.fake.lro", 0, Msg::kNetSysctlWhyLro},
    {"net.link.fake.trailer_length", 0, Msg::kNetSysctlWhyTrailer},
    {"net.link.fake.separate_frame_header", 0, Msg::kNetSysctlWhySeparateHeader},
};
}  // namespace multitether::net
