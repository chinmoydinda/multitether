#include "multitether/net/bpf_link.h"

#include <fcntl.h>
#include <net/bpf.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>

#include "multitether/common/byte_order.h"
#include "multitether/common/i18n.h"
#include "multitether/common/logging.h"
#include "multitether/net/darwin_abi.h"
#include "multitether/net/feth_device.h"

namespace multitether::net {
namespace {
constexpr int kMaxBpfDeviceIndex = 256;

constexpr std::size_t kMaxBatchWriteBytes = std::size_t{256} * 1024;

[[nodiscard]] Status CallIoctl(int fd, unsigned long request, void* argument,
                               std::string_view what) {
  if (::ioctl(fd, request, argument) < 0) {
    return std::unexpected(Error::FromErrno(0, std::string{what}));
  }
  return Ok();
}

[[nodiscard]] bool TryOptionalIoctl(int fd, unsigned long request, void* argument,
                                    std::string_view what) {
  if (::ioctl(fd, request, argument) < 0) {
    MULTITETHER_DEBUG_TR(Msg::kNetOptionalIoctlUnavailable, what, errno);
    return false;
  }
  return true;
}
}  // namespace

BpfLink::~BpfLink() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void BpfLink::Interrupt() noexcept {
  interrupted_.store(true, std::memory_order_release);
}

Result<std::unique_ptr<BpfLink>> BpfLink::Open(std::string_view interface_name,
                                              const BpfConfig& config) {
  if (interface_name.size() >= kInterfaceNameCapacity) {
    return std::unexpected(Error::Generic(Tr(Msg::kNetInterfaceNameTooLong, interface_name)));
  }
  if (config.max_frame_bytes < kMinEthernetFrameBytes) {
    return std::unexpected(Error::Generic(
        Tr(Msg::kNetFrameLimitBelowMinimum, config.max_frame_bytes, kMinEthernetFrameBytes)));
  }

  auto link = std::unique_ptr<BpfLink>(new BpfLink());
  link->interface_name_ = interface_name;
  link->max_frame_bytes_ = config.max_frame_bytes;

  int last_errno = 0;
  for (int index = 0; index < kMaxBpfDeviceIndex; ++index) {
    std::string path = std::format("/dev/bpf{}", index);
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
      link->fd_ = fd;
      link->device_path_ = std::move(path);
      break;
    }
    last_errno = errno;
    if (last_errno == EBUSY) {
      continue;
    }
    if (last_errno == ENOENT) {
      break;
    }
    if (last_errno == EACCES || last_errno == EPERM) {
      return std::unexpected(
          Error::FromErrno(last_errno, Tr(Msg::kNetBpfOpenDenied, path)));
    }
  }

  if (link->fd_ < 0) {
    return std::unexpected(
        Error::FromErrno(last_errno, Tr(Msg::kNetBpfNoDeviceAvailable)));
  }

  auto buffer_bytes = static_cast<unsigned int>(config.kernel_buffer_bytes);
  MULTITETHER_RETURN_IF_ERROR(
      CallIoctl(link->fd_, BIOCSBLEN, &buffer_bytes, "ioctl(BIOCSBLEN)"));
  link->kernel_buffer_bytes_ = buffer_bytes;
  if (buffer_bytes != config.kernel_buffer_bytes) {
    MULTITETHER_INFO_TR(Msg::kNetBpfBufferClamped, config.kernel_buffer_bytes, buffer_bytes);
  }

  int header_complete = 1;
  MULTITETHER_RETURN_IF_ERROR(
      CallIoctl(link->fd_, BIOCSHDRCMPLT, &header_complete, "ioctl(BIOCSHDRCMPLT)"));

  ::ifreq bind_request{};
  std::memcpy(bind_request.ifr_name, interface_name.data(), interface_name.size());
  if (const auto status =
          CallIoctl(link->fd_, BIOCSETIF, &bind_request, "ioctl(BIOCSETIF)");
      !status) {
    Error error = status.error();
    if (error.Code() == ENXIO) {
      return std::unexpected(std::move(error).WithContext(
          Tr(Msg::kNetBpfInterfaceMissing, interface_name)));
    }
    return std::unexpected(
        std::move(error).WithContext(Tr(Msg::kNetBpfBindFailed, interface_name)));
  }

  unsigned int data_link_type = 0;
  MULTITETHER_RETURN_IF_ERROR(
      CallIoctl(link->fd_, BIOCGDLT, &data_link_type, "ioctl(BIOCGDLT)"));
  if (data_link_type != DLT_EN10MB) {
    return std::unexpected(Error::Generic(
        Tr(Msg::kNetBpfWrongDataLinkType, interface_name, data_link_type, DLT_EN10MB)));
  }

  unsigned int immediate = 1;
  MULTITETHER_RETURN_IF_ERROR(
      CallIoctl(link->fd_, BIOCIMMEDIATE, &immediate, "ioctl(BIOCIMMEDIATE)"));

  unsigned int see_sent = 0;
  MULTITETHER_RETURN_IF_ERROR(
      CallIoctl(link->fd_, BIOCSSEESENT, &see_sent, "ioctl(BIOCSSEESENT)"));

  ::timeval read_timeout{};
  read_timeout.tv_sec = config.read_timeout_millis / 1000;
  read_timeout.tv_usec = static_cast<__darwin_suseconds_t>(
      (config.read_timeout_millis % 1000) * 1000);
  MULTITETHER_RETURN_IF_ERROR(
      CallIoctl(link->fd_, BIOCSRTIMEOUT, &read_timeout, "ioctl(BIOCSRTIMEOUT)"));

  if (config.try_batch_write) {
    int enable = 1;
    link->batch_write_enabled_ =
        TryOptionalIoctl(link->fd_, kBpfSetBatchWrite, &enable, "BIOCSBATCHWRITE");
  }
  if (config.try_disable_timestamp) {
    int disable = 1;
    link->timestamp_disabled_ =
        TryOptionalIoctl(link->fd_, kBpfSetNoTimestamp, &disable, "BIOCSNOTSTAMP");
  }

  link->read_buffer_.resize(link->kernel_buffer_bytes_);
  link->read_frames_.reserve(config.max_frames_per_batch);
  if (link->batch_write_enabled_) {
    link->write_buffer_.resize(kMaxBatchWriteBytes);
  }

  MULTITETHER_INFO_TR(Msg::kNetBpfReady, link->device_path_, link->interface_name_,
                    link->kernel_buffer_bytes_ / 1024, link->max_frame_bytes_,
                    Text(link->batch_write_enabled_ ? Msg::kNetBpfBatchWriteEnabled
                                                    : Msg::kNetBpfBatchWriteUnavailable),
                    Text(link->timestamp_disabled_ ? Msg::kNetBpfTimestampDisabled
                                                   : Msg::kNetBpfTimestampEnabled));

  return link;
}

Result<BpfLink::KernelStats> BpfLink::QueryKernelStats() const {
  ::bpf_stat stats{};
  if (::ioctl(fd_, BIOCGSTATS, &stats) < 0) {
    return std::unexpected(Error::FromErrno(0, "ioctl(BIOCGSTATS)"));
  }
  return KernelStats{.received = stats.bs_recv, .dropped = stats.bs_drop};
}

Result<ReadBatch> BpfLink::ReadFrames() {
  read_frames_.clear();

  if (interrupted_.load(std::memory_order_acquire)) {
    return ReadBatch{.kernel_drops = last_kernel_drops_};
  }

  // Darwin BPF requires the read buffer size to equal BIOCGBLEN/BIOCSBLEN (bd_bufsize).
  const ssize_t received = ::read(fd_, read_buffer_.data(), read_buffer_.size());
  if (received < 0) {
    if (errno == EINTR || errno == EAGAIN) {
      return ReadBatch{.kernel_drops = last_kernel_drops_};
    }
    return std::unexpected(Error::FromErrno(0, Tr(Msg::kNetBpfReadFailed, device_path_)));
  }
  if (received == 0) {
    return ReadBatch{.kernel_drops = last_kernel_drops_};
  }

  const auto total = static_cast<std::size_t>(received);
  std::size_t offset = 0;

  while (offset + kBpfHeaderMinBytes <= total) {
    const std::byte* record = read_buffer_.data() + offset;

    const std::uint32_t capture_length = LoadLe32(record + offsetof(::bpf_hdr, bh_caplen));
    const std::uint32_t original_length = LoadLe32(record + offsetof(::bpf_hdr, bh_datalen));
    const std::uint32_t header_length = LoadLe16(record + offsetof(::bpf_hdr, bh_hdrlen));

    if (header_length < kBpfHeaderMinBytes) [[unlikely]] {
      MULTITETHER_WARN_TR(Msg::kNetBpfHeaderTooShort, header_length, kBpfHeaderMinBytes);
      break;
    }

    const std::size_t record_bytes = static_cast<std::size_t>(header_length) + capture_length;
    if (offset + record_bytes > total) [[unlikely]] {
      MULTITETHER_WARN_TR(Msg::kNetBpfRecordOutOfBounds, offset, record_bytes, total);
      break;
    }

    if (capture_length == original_length && capture_length >= kMinEthernetFrameBytes &&
        capture_length <= max_frame_bytes_) [[likely]] {
      read_frames_.push_back(FrameView{.data = record + header_length, .length = capture_length});
    } else [[unlikely]] {
      MULTITETHER_TRACE_TR(Msg::kNetBpfSkipOversizedRecord, capture_length, original_length,
                         max_frame_bytes_);
    }

    offset += BPF_WORDALIGN(record_bytes);

    if (read_frames_.size() >= read_frames_.capacity()) {
      MULTITETHER_WARN_TR(Msg::kNetBpfBatchFrameLimit, read_frames_.capacity(), total - offset);
      break;
    }
  }

  if (const auto stats = QueryKernelStats()) {
    last_kernel_drops_ = stats->dropped;
  }
  return ReadBatch{
      .frames = read_frames_,
      .kernel_drops = last_kernel_drops_,
  };
}

Result<WriteResult> BpfLink::WriteFrames(FrameBatch frames) {
  if (frames.empty()) {
    return WriteResult{};
  }
  return batch_write_enabled_ ? WriteFramesBatched(frames) : WriteFramesIndividually(frames);
}

Result<WriteResult> BpfLink::WriteFramesIndividually(FrameBatch frames) {
  WriteResult result;
  for (const FrameView& frame : frames) {
    if (frame.length < kMinEthernetFrameBytes || frame.length > max_frame_bytes_) [[unlikely]] {
      ++result.frames_skipped;
      continue;
    }
    const ssize_t written = ::write(fd_, frame.data, frame.length);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }

      if (errno == ENOBUFS || errno == EAGAIN) {
        break;
      }
      return std::unexpected(
          Error::FromErrno(0, Tr(Msg::kNetBpfWriteFailed, device_path_, frame.length)));
    }
    ++result.frames_written;
    result.bytes_written += static_cast<std::uint64_t>(written);
  }
  return result;
}

Result<WriteResult> BpfLink::WriteFramesBatched(FrameBatch frames) {
  WriteResult result;
  std::size_t cursor = 0;
  std::uint32_t pending_frames = 0;
  std::uint64_t pending_bytes = 0;

  const auto flush = [&]() -> Status {
    if (cursor == 0) {
      return Ok();
    }
    const ssize_t written = ::write(fd_, write_buffer_.data(), cursor);
    if (written < 0) {
      if (errno == ENOBUFS || errno == EAGAIN || errno == EINTR) {
        cursor = 0;
        pending_frames = 0;
        pending_bytes = 0;
        return Ok();
      }
      return std::unexpected(Error::FromErrno(
          0, Tr(Msg::kNetBpfBatchWriteFailed, device_path_, pending_frames, cursor)));
    }
    result.frames_written += pending_frames;
    result.bytes_written += pending_bytes;
    cursor = 0;
    pending_frames = 0;
    pending_bytes = 0;
    return Ok();
  };

  for (const FrameView& frame : frames) {
    if (frame.length < kMinEthernetFrameBytes || frame.length > max_frame_bytes_) [[unlikely]] {
      ++result.frames_skipped;
      continue;
    }

    const std::size_t record_bytes = kBpfHeaderMinBytes + frame.length;
    const std::size_t aligned_bytes = BPF_WORDALIGN(record_bytes);
    if (cursor + aligned_bytes > write_buffer_.size()) {
      MULTITETHER_RETURN_IF_ERROR(flush());
      if (aligned_bytes > write_buffer_.size()) [[unlikely]] {
        ++result.frames_skipped;
        continue;
      }
    }

    std::byte* record = write_buffer_.data() + cursor;

    std::memset(record, 0, kBpfHeaderMinBytes);
    StoreLe32(record + offsetof(::bpf_hdr, bh_caplen), frame.length);
    StoreLe32(record + offsetof(::bpf_hdr, bh_datalen), frame.length);
    StoreLe16(record + offsetof(::bpf_hdr, bh_hdrlen),
              static_cast<std::uint16_t>(kBpfHeaderMinBytes));
    std::memcpy(record + kBpfHeaderMinBytes, frame.data, frame.length);

    if (aligned_bytes > record_bytes) {
      std::memset(record + record_bytes, 0, aligned_bytes - record_bytes);
    }

    cursor += aligned_bytes;
    ++pending_frames;
    pending_bytes += frame.length;
  }

  MULTITETHER_RETURN_IF_ERROR(flush());
  return result;
}
}  // namespace multitether::net
