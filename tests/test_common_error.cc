#include <cerrno>
#include <string>

#include <doctest.h>

#include "multitether/common/error.h"
#include "multitether/common/logging.h"

using multitether::Error;
using multitether::ErrorDomain;
using multitether::LogLevel;
using multitether::Ok;
using multitether::Result;
using multitether::Status;

namespace {
Status FailingStep() {
  return std::unexpected(Error::FromErrno(ENOENT, "opening /dev/bpf0 failed"));
}

Status SucceedingStep() {
  return Ok();
}

Status PipelineWithFailure() {
  MULTITETHER_RETURN_IF_ERROR(SucceedingStep());
  MULTITETHER_RETURN_IF_ERROR(FailingStep());
  return Ok();
}

Result<int> ProducingStep(bool succeed) {
  if (!succeed) {
    return std::unexpected(Error::Generic("injected failure"));
  }
  return 42;
}

Result<int> ConsumingPipeline(bool succeed) {
  MULTITETHER_ASSIGN_OR_RETURN(const int value, ProducingStep(succeed));
  return value * 2;
}
}  // namespace

TEST_SUITE("common.error") {
TEST_CASE("common_error scenario 1: errno") {
  const Error error = Error::FromErrno(EACCES, "opening BPF failed");
  CHECK(error.Domain() == ErrorDomain::kErrno);
  CHECK(error.Code() == EACCES);
  const std::string text = error.ToString();
  CHECK(text.find("opening BPF failed") != std::string::npos);
  CHECK(text.find("errno") != std::string::npos);
  CHECK(text.find(std::to_string(EACCES)) != std::string::npos);
}

TEST_CASE("common_error scenario 2: errno errno") {
  errno = EPERM;
  const Error error = Error::FromErrno(0, "creating feth failed");
  CHECK(error.Code() == EPERM);
}

TEST_CASE("common_error scenario 3: libusb libusb") {
  const Error error = Error::FromLibUsb(-3, "claiming RNDIS control interface failed");
  CHECK(error.Domain() == ErrorDomain::kLibUsb);
  const std::string text = error.ToString();
  CHECK(text.find("LIBUSB_ERROR_ACCESS") != std::string::npos);
}

TEST_CASE("common_error scenario 4: RNDIS") {
  const Error error = Error::FromRndisStatus(0xC00000BBU, "querying OID failed");
  const std::string text = error.ToString();
  CHECK(text.find("querying OID failed") != std::string::npos);
  CHECK(text.find("0xc00000bb") != std::string::npos);
  CHECK(error.Domain() == ErrorDomain::kRndis);
  CHECK(error.Code() == 0xC00000BB);
}

TEST_CASE("common_error scenario 5: WithContext") {
  Error inner = Error::FromErrno(ENODEV, "ioctl(BIOCSETIF) failed");
  const Error outer = std::move(inner).WithContext("binding BPF to feth1");
  const std::string text = outer.ToString();
  const auto outer_pos = text.find("binding BPF to feth1");
  const auto inner_pos = text.find("ioctl(BIOCSETIF) failed");
  REQUIRE(outer_pos != std::string::npos);
  REQUIRE(inner_pos != std::string::npos);
  CHECK(outer_pos < inner_pos);
}

TEST_CASE("common_error scenario 6: RETURN_IF_ERROR") {
  const Status status = PipelineWithFailure();
  REQUIRE_FALSE(status.has_value());
  CHECK(status.error().Code() == ENOENT);
}

TEST_CASE("common_error scenario 7: ASSIGN_OR_RETURN") {
  const Result<int> good = ConsumingPipeline(true);
  REQUIRE(good.has_value());
  CHECK(good.value() == 84);

  const Result<int> bad = ConsumingPipeline(false);
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error().Context() == "injected failure");
}

TEST_CASE("common_error scenario 8") {
  const LogLevel original = multitether::GetLogLevel();

  multitether::SetLogLevel(LogLevel::kWarn);
  CHECK(multitether::GetLogLevel() == LogLevel::kWarn);
  CHECK_FALSE(multitether::detail::IsLogLevelEnabled(LogLevel::kInfo));
  CHECK(multitether::detail::IsLogLevelEnabled(LogLevel::kWarn));
  CHECK(multitether::detail::IsLogLevelEnabled(LogLevel::kError));

  multitether::SetLogLevel(LogLevel::kOff);
  CHECK_FALSE(multitether::detail::IsLogLevelEnabled(LogLevel::kError));

  multitether::SetLogLevel(original);
}

TEST_CASE("common_error scenario 9: __FILE__") {
  CHECK(multitether::detail::BaseName("/a/b/c/bpf_link.cc") == "bpf_link.cc");
  CHECK(multitether::detail::BaseName("bpf_link.cc") == "bpf_link.cc");
  CHECK(multitether::detail::BaseName("") == "");
}
}  // TEST_SUITE("common.error")
