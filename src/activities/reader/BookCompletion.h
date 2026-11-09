#pragma once

#include <optional>
#include <string>

namespace readingstats {
std::optional<bool> bookCompletion(const std::string& bookPath);
}
