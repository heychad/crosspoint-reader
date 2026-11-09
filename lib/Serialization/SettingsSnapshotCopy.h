#pragma once

namespace settingsrecovery {
enum class CopyResult { Copied, Refused, Failed };

// Experimental: never overwrite an existing destination. A failed operation
// may leave a newly created partial snapshot, which is preserved for inspection.
// A reported copy requires distinct single-cluster chains; it does not validate
// the entire filesystem or recover settings.
CopyResult copySnapshot(const char* source, const char* destination);
}  // namespace settingsrecovery
