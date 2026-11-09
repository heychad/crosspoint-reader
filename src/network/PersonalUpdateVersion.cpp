#include "PersonalUpdateVersion.h"

#include <array>
#include <cstdint>
#include <limits>

namespace {
struct Version {
  std::array<uint32_t, 4> parts{};
};

bool number(std::string_view& input, uint32_t& output) {
  if (input.empty() || input.front() < '0' || input.front() > '9') return false;
  output = 0;
  do {
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

bool parse(std::string_view input, bool current, Version& version) {
  if (!number(input, version.parts[0]) || !consume(input, ".") || !number(input, version.parts[1]) ||
      !consume(input, ".") || !number(input, version.parts[2]) || !consume(input, "-chad.") ||
      !number(input, version.parts[3]))
    return false;
  if (!current) return input.empty();
  if (input.starts_with("-test")) {
    input.remove_prefix(5);
    uint32_t testNumber = 0;
    if (!number(input, testNumber)) return false;
  }
  return input.empty() || (consume(input, "-x4pro") && input.empty());
}
}  // namespace

bool personalupdate::isNewer(std::string_view releaseTag, std::string_view currentVersion) {
  Version release;
  Version current;
  if (!parse(releaseTag, false, release) || !parse(currentVersion, true, current)) return false;
  return release.parts > current.parts;
}
