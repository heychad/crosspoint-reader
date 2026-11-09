#pragma once

#include <string>

namespace booktracking {
// Unknown means the card could not establish every marker's state safely.
enum class State { Enabled, Disabled, Unknown };
State state(const std::string& cachePath, const std::string& bookPath);
// Changes the next reader session. Existing statistics and positions are untouched.
bool setEnabled(const std::string& cachePath, const std::string& bookPath, bool enabled);
}  // namespace booktracking
