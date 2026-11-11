#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct BookReadingStats;

namespace statsbackup {
// Back up the exact verified saved payload for one book without replacing prior backups.
bool createBookBackup(const std::string& cachePath, const BookReadingStats& stats);
// Read a backup for this book. Filename hash detects accidental corruption;
// it does not authenticate the backup or prove a completed disk write.
bool readBookBackup(const std::string& cachePath, const std::string& filename, BookReadingStats& output);
enum class BookRestoreResult { Restored, Unavailable, Busy, BackupFailed, WriteUncertain, RecoveryRequired };
// Prototype; a completion change uses the recoverable book/global journal.
BookRestoreResult restoreBookBackup(const std::string& cachePath, const std::string& bookPath,
                                    const std::string& filename, const uint8_t* expectedCurrent = nullptr,
                                    size_t expectedCurrentSize = 0, const uint8_t* expectedBackup = nullptr,
                                    size_t expectedBackupSize = 0);
}  // namespace statsbackup
