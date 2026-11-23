#include "PersonalUpdateVersion.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
struct Version {
  std::array<uint32_t, 4> parts{};
};

bool number(std::string_view& input, uint32_t& output, size_t maxDigits = 10) {
  if (input.empty() || input.front() < '0' || input.front() > '9') return false;
  output = 0;
  size_t digits = 0;
  do {
    if (++digits > maxDigits) return false;
    const auto digit = static_cast<uint32_t>(input.front() - '0');
    if (output > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
    output = output * 10 + digit;
    input.remove_prefix(1);
  } while (!input.empty() && input.front() >= '0' && input.front() <= '9');
  return true;
}

bool consume(std::string_view& input, std::string_view prefix) {
  if (!input.starts_with(prefix)) return false;
  input.remove_prefix(prefix.size());
  return true;
}

bool validTestLabel(std::string_view label) {
  return !label.empty() && label.front() != '-' && label.back() != '-' && label.find("--") == std::string_view::npos &&
         std::all_of(label.begin(), label.end(),
                     [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

bool parse(std::string_view input, bool current, Version& version) {
  if (!current && input.size() > 26) return false;
  const size_t digits = current ? 10 : 9;
  if (!number(input, version.parts[0], digits) || !consume(input, ".") || !number(input, version.parts[1], digits) ||
      !consume(input, ".") || !number(input, version.parts[2], digits) || !consume(input, "-chad.") ||
      !number(input, version.parts[3], digits))
    return false;
  if (!current) return input.empty();

  constexpr std::string_view boardSuffix = "-x4pro";
  const bool x4pro = input.ends_with(boardSuffix);
  if (x4pro) input.remove_suffix(boardSuffix.size());
  if (input.empty()) return true;

  uint32_t testNumber = 0;
  if (!consume(input, "-test") || !number(input, testNumber)) return false;
  return input.empty() || (x4pro && consume(input, "-") && validTestLabel(input));
}
}  // namespace

bool personalupdate::isReleaseTag(const std::string_view releaseTag) {
  Version version;
  return parse(releaseTag, false, version);
}

bool personalupdate::isNewer(std::string_view releaseTag, std::string_view currentVersion) {
  Version release;
  Version current;
  if (!parse(releaseTag, false, release) || !parse(currentVersion, true, current)) return false;
  return release.parts > current.parts;
}
