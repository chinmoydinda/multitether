#include <cctype>
#include <cstddef>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "doctest.h"
#include "multitether/common/error.h"
#include "multitether/common/i18n.h"

namespace {
using multitether::Msg;

constexpr std::size_t kMessageCount = static_cast<std::size_t>(Msg::kMessageCount);

struct ParsedFormat {
  std::map<std::size_t, std::string> types;

  bool mixed_indexing = false;

  bool malformed = false;
};

[[nodiscard]] std::string PresentationType(std::string_view spec) {
  if (spec.empty()) {
    return {};
  }
  const char last = spec.back();
  return (std::isalpha(static_cast<unsigned char>(last)) != 0) ? std::string{last} : std::string{};
}

[[nodiscard]] ParsedFormat ParseFormat(std::string_view text) {
  ParsedFormat parsed;
  std::size_t auto_index = 0;
  bool saw_auto = false;
  bool saw_manual = false;

  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '}') {
      if (i + 1 < text.size() && text[i + 1] == '}') {
        ++i;
      }
      continue;
    }
    if (text[i] != '{') {
      continue;
    }
    if (i + 1 < text.size() && text[i + 1] == '{') {
      ++i;
      continue;
    }

    const std::size_t close = text.find('}', i);
    if (close == std::string_view::npos) {
      parsed.malformed = true;
      break;
    }
    const std::string_view body = text.substr(i + 1, close - i - 1);
    i = close;

    const std::size_t colon = body.find(':');
    const std::string_view index_text = body.substr(0, colon);
    const std::string_view spec =
        colon == std::string_view::npos ? std::string_view{} : body.substr(colon + 1);
    if (spec.find('{') != std::string_view::npos) {
      parsed.malformed = true;
      continue;
    }

    std::size_t index = 0;
    if (index_text.empty()) {
      saw_auto = true;
      index = auto_index++;
    } else {
      saw_manual = true;
      for (const char c : index_text) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
          parsed.malformed = true;
          break;
        }
        index = (index * 10) + static_cast<std::size_t>(c - '0');
      }
    }
    parsed.types[index] = PresentationType(spec);
  }

  parsed.mixed_indexing = saw_auto && saw_manual;
  return parsed;
}

[[nodiscard]] std::string Label(std::size_t index) {
  const std::string_view english = multitether::Text(static_cast<Msg>(index));
  return std::format("message #{} (\"{}\")", index, english.substr(0, 48));
}
}  // namespace

TEST_SUITE("common.i18n") {
TEST_CASE("English messages have valid contiguous format arguments") {
  for (std::size_t i = 0; i < kMessageCount; ++i) {
    const auto text = multitether::Text(static_cast<Msg>(i));
    CAPTURE(Label(i));
    CHECK_FALSE(text.empty());
    const auto parsed = ParseFormat(text);
    CHECK_FALSE(parsed.malformed);
    CHECK_FALSE(parsed.mixed_indexing);
    std::size_t expected = 0;
    for (const auto& [index, type] : parsed.types) { CHECK(index == expected); ++expected; }
  }
}
TEST_CASE("Formatting and invalid message IDs fail safely") {
  CHECK(multitether::Text(Msg::kCoreStopped) == "stopped");
  CHECK(multitether::Tr(Msg::kNetBpfBindFailed, "feth1").find("feth1") != std::string::npos);
  CHECK(multitether::Tr(Msg::kNetBpfBindFailed) == multitether::Text(Msg::kNetBpfBindFailed));
  CHECK(multitether::Text(Msg::kMessageCount).empty());
  const auto error = multitether::Error::Generic("inner").WithContext("outer");
  CHECK(error.Context().starts_with("outer: "));
}
}
