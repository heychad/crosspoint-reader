#pragma once

struct GlobalReadingStats;

namespace statsbackup {
// Creates one verified, non-overwriting device-statistics backup on the SD card.
bool createDeviceBackup(const GlobalReadingStats& stats);
}  // namespace statsbackup
