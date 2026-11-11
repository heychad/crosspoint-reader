#pragma once

namespace readingstats::completionmove {
// The library builder treats this persisted marker as a request to rebuild.
// Mark before moving files so a restart cannot retain the old index silently.
bool markLibraryDirtyForMove();
}  // namespace readingstats::completionmove
