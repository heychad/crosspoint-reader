#pragma once

namespace readingstats {
// Caller holds the recursive storage lock throughout the guarded operation.
// Refuses ordinary reads/writes if a completion needs recovery or cannot be inspected.
bool normalStatsAllowed();
}  // namespace readingstats
