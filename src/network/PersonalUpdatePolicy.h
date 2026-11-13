#pragma once

#include <string_view>

namespace personalupdate {
bool isPersonalImageUrl(std::string_view url);
bool isAssetRedirect(std::string_view url);
}  // namespace personalupdate
