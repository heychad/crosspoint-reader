#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct GlobalReadingStats;
struct ReadingStatsDate;

namespace statsbackup {
// Creates one verified, non-overwriting device-statistics backup on the SD card.
bool createDeviceBackup(const GlobalReadingStats& stats);
enum class AutomaticBackupResult { Saved, AlreadyPresent, Unavailable };
// Preserve a snapshot without overwriting any existing backup. An all-zero
// date creates a separately named undated snapshot; other invalid dates fail.
AutomaticBackupResult createAutomaticDeviceBackup(const ReadingStatsDate& date);
enum class BackupIntegrity { Unavailable, Checked, LegacyUnverified };
// Older CrossInk backups have no checksum. Checked uses a noncryptographic hash
// and does not prove that the original write reached stable storage.
BackupIntegrity inspectDeviceBackup(const std::string& filename, GlobalReadingStats& output);
// A failure leaves output untouched and never changes files.
bool readDeviceBackup(const std::string& filename, GlobalReadingStats& output);

enum class RestoreResult { Restored, Unavailable, Busy, BackupFailed, WriteUncertain };
// Caller must obtain explicit confirmation before invoking this operation.
// A successful restore replaces device totals, not book statistics or progress.
RestoreResult restoreDeviceBackup(const std::string& filename, const uint8_t* expectedCurrent = nullptr,
                                  size_t expectedSize = 0, const uint8_t* expectedBackup = nullptr,
                                  size_t expectedBackupSize = 0);
// Clear local device totals after preserving a separate backup. The caller must
// confirm the action and may supply the exact totals shown for confirmation.
RestoreResult resetDeviceStats(const uint8_t* expectedCurrent = nullptr, size_t expectedSize = 0);
}  // namespace statsbackup
