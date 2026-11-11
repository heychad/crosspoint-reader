#pragma once

#include <cstdint>
#include <string>

namespace legacybookmarkedit {
enum class Result { Ready, Published, Archived, Restored, Conflict, Invalid, Unavailable, Uncertain };
// Staging, backup, and a separately verified original are retained.
// Existing artifacts block a new edit.
// These operations do not establish full-volume allocation ownership.
Result stage(const std::string& source, const std::string& bookPath, uint16_t first, uint16_t count);
Result publish(const std::string& source, const std::string& bookPath, uint16_t first, uint16_t count);
// Preserve both original copies in one of 64 history slots after a verified edit.
Result archivePublished(const std::string& source, const std::string& bookPath, uint16_t first, uint16_t count);
// Restores only a missing primary from a validated backup; never removes staging.
Result restoreOriginal(const std::string& source, const std::string& bookPath);
}  // namespace legacybookmarkedit
