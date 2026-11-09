#pragma once

#include <string>

struct BookReadingStats;

namespace statsbackup {
// Back up the exact verified saved payload for one book without replacing prior backups.
bool createBookBackup(const std::string& cachePath, const BookReadingStats& stats);
}  // namespace statsbackup
