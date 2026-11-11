#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace crossinkimport {
using ImportMarker = std::array<uint8_t, 32>;
enum class GuardState { Clear, NeedsReview, Unavailable };
enum class RecoveryResult { Recovered, NotNeeded, NeedsReview, Unavailable };
// These operations are serialized with the storage lock. Marker files are
// never removed or reused by the firmware.
GuardState inspectImports();
bool beginImport(const char* snapshot, const char* before, size_t beforeSize, const char* after, size_t afterSize,
                 ImportMarker& marker);
bool finishImport(const ImportMarker& marker);
// Resolves exactly one interrupted import only when primary and snapshot are
// byte-identical and no settings recovery artifacts are present.
RecoveryResult recoverUnchangedImport();
}  // namespace crossinkimport
