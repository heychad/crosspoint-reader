#pragma once

namespace settingsrecovery {
enum class CopyResult { Copied, Refused, Failed };

// Experimental: never overwrite an existing destination. A failed operation
// may leave a newly created partial snapshot, which is preserved for inspection.
// This does not establish allocation-chain independence or recover settings.
CopyResult copySnapshot(const char* source, const char* destination);
}  // namespace settingsrecovery
