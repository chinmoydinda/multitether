#include <doctest.h>

#include "multitether/version.h"

TEST_SUITE("version") {
TEST_CASE("Version is nonzero") {
  const auto v = multitether::GetVersion();
  CHECK((v.major != 0 || v.minor != 0 || v.patch != 0));
}

TEST_CASE("Version string identifies MultiTether") {
  const std::string_view s = multitether::GetVersionString();
  CHECK(s.find("MultiTether") != std::string_view::npos);
}
}  // TEST_SUITE("version")
