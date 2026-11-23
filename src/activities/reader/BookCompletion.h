#pragma once

#include <optional>
#include <string>

namespace readingstats {
// Used by Library filtering. Pending recovery and unreadable state are unknown.
std::optional<bool> bookCompletion(const std::string& bookPath);
}  // namespace readingstats
