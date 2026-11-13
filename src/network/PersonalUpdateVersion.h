#pragma once

#include <string_view>

namespace personalupdate {
inline constexpr char RELEASE_API[] = "https://api.github.com/repos/heychad/crosspoint-reader/releases/latest";
// Enable only after peer, hostname, redirect, and firmware authenticity are verified.
inline constexpr bool AUTHENTICATED_TRANSPORT = false;
// Release tags are X.Y.Z-chad.N. Current X4 Pro builds may append -testN
// and/or -x4pro. Updating requires a strictly greater numbered release.
bool isNewer(std::string_view releaseTag, std::string_view currentVersion);
bool isReleaseTag(std::string_view releaseTag);
}  // namespace personalupdate
