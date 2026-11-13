#include "PersonalUpdatePolicy.h"

#include "PersonalUpdateVersion.h"

namespace {
bool safeUrl(const std::string_view url) {
  if (url.size() > 2048) return false;
  for (const unsigned char c : url)
    if (c < 0x21 || c > 0x7e || c == '\\' || c == '#') return false;
  return true;
}
}  // namespace

bool personalupdate::isPersonalImageUrl(const std::string_view url) {
  constexpr std::string_view PREFIX = "https://github.com/heychad/crosspoint-reader/releases/download/";
  constexpr std::string_view NAME = "crosspoint-", SUFFIX = "-x4pro.bin";
  if (!safeUrl(url) || !url.starts_with(PREFIX)) return false;
  const auto rest = url.substr(PREFIX.size());
  const auto slash = rest.find('/');
  if (slash == std::string_view::npos) return false;
  const auto tag = rest.substr(0, slash), filename = rest.substr(slash + 1);
  return isReleaseTag(tag) && filename.size() == NAME.size() + tag.size() + SUFFIX.size() &&
         filename.starts_with(NAME) && filename.ends_with(SUFFIX) && filename.substr(NAME.size(), tag.size()) == tag;
}

bool personalupdate::isAssetRedirect(const std::string_view url) {
  constexpr std::string_view PREFIX = "https://release-assets.githubusercontent.com/";
  return safeUrl(url) && url.starts_with(PREFIX) && url.size() > PREFIX.size() && url[PREFIX.size()] != '?';
}
