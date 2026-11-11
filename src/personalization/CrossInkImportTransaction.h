#pragma once

#include <ArduinoJson.h>

#include "CrossInkImportSnapshot.h"

namespace crossinkimport {
enum class ImportKind { Controls, ReaderBars, Library, Preferences };
enum class ApplyResult {
  Published,
  NoChanges,
  SourceMissing,
  SourceRecovery,
  InvalidSource,
  Unsupported,
  Unavailable,
  SnapshotFailed,
  RecoveryRequired,
  WriteUncertain
};
// Caller holds the settings mutex, supplies the exact previously read primary
// bytes and a staging document, and discards the document unless published.
// WriteUncertain requires recovery inspection before another settings write.
ApplyResult publishControls(JsonDocument& proposed, bool replaceExisting, bool currentQuickTrigger,
                            const char* expectedPrimary, size_t expectedSize, char (&snapshot)[SNAPSHOT_PATH_SIZE]);
ApplyResult publishReaderBars(JsonDocument& proposed, const char* expectedPrimary, size_t expectedSize,
                              char (&snapshot)[SNAPSHOT_PATH_SIZE]);
ApplyResult publishLibrary(JsonDocument& proposed, const char* expectedPrimary, size_t expectedSize,
                           char (&snapshot)[SNAPSHOT_PATH_SIZE]);
ApplyResult publishPreferences(JsonDocument& proposed, const char* expectedPrimary, size_t expectedSize,
                               char (&snapshot)[SNAPSHOT_PATH_SIZE]);
}  // namespace crossinkimport
