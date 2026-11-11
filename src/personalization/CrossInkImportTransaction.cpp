#include "CrossInkImportTransaction.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SettingsSafeWrite.h>

#include "CrossInkBarImport.h"
#include "CrossInkControlImport.h"
#include "CrossInkImportGuard.h"
#include "CrossInkLibraryImport.h"
#include "CrossInkPreferencesImport.h"
#include "CrossInkSettingsSource.h"

namespace crossinkimport {
namespace {
constexpr size_t MAX_BYTES = 32768;
struct Context {
  JsonDocument& proposed;
  ImportKind kind;
  bool replaceExisting;
  bool currentQuickTrigger;
  const char* expectedPrimary;
  size_t expectedSize;
  char (&snapshot)[SNAPSHOT_PATH_SIZE];
  ApplyResult result = ApplyResult::Unavailable;
};

bool publishLocked(void* raw) {
  auto& context = *static_cast<Context*>(raw);
  const auto guard = inspectImports();
  if (guard == GuardState::Unavailable) return true;
  if (guard == GuardState::NeedsReview) {
    context.result = ApplyResult::RecoveryRequired;
    return true;
  }
  JsonDocument source;
  switch (readSource(source)) {
    case SourceResult::Missing:
      context.result = ApplyResult::SourceMissing;
      return true;
    case SourceResult::RecoveryPresent:
      context.result = ApplyResult::SourceRecovery;
      return true;
    case SourceResult::Invalid:
      context.result = ApplyResult::InvalidSource;
      return true;
    case SourceResult::Unavailable:
      return true;
    case SourceResult::Loaded:
      break;
  }
  bool changed = false;
  switch (context.kind) {
    case ImportKind::Controls:
      changed = stageControls(source.as<JsonVariantConst>(), context.proposed, context.replaceExisting,
                              context.currentQuickTrigger);
      break;
    case ImportKind::Library:
      changed = stageLibrary(source.as<JsonVariantConst>(), context.proposed, context.replaceExisting);
      break;
    case ImportKind::Preferences:
      changed = stagePreferences(source.as<JsonVariantConst>(), context.proposed, context.replaceExisting);
      break;
    case ImportKind::ReaderBars:
      switch (stageBars(source.as<JsonVariantConst>(), context.proposed)) {
        case BarResult::Imported:
          changed = true;
          break;
        case BarResult::Unchanged:
          break;
        case BarResult::Missing:
          context.result = ApplyResult::SourceMissing;
          return true;
        case BarResult::Unsupported:
          context.result = ApplyResult::Unsupported;
          return true;
        case BarResult::Invalid:
          context.result = ApplyResult::InvalidSource;
          return true;
        case BarResult::Unavailable:
          return true;
      }
      break;
  }
  source.clear();
  if (context.proposed.overflowed()) return true;
  if (!changed) {
    context.result = ApplyResult::NoChanges;
    return true;
  }
  if (!snapshotCurrentSettings(context.expectedPrimary, context.expectedSize, context.snapshot)) {
    context.result = ApplyResult::SnapshotFailed;
    return true;
  }
  const size_t size = measureJson(context.proposed);
  if (!size || size > MAX_BYTES) return true;
  auto bytes = makeUniqueNoThrow<char[]>(size + 1);
  if (!bytes) {
    LOG_ERR("CROSSINK", "Could not allocate %u bytes for imported settings", static_cast<unsigned>(size + 1));
    return true;
  }
  if (serializeJson(context.proposed, bytes.get(), size + 1) != size) return true;
  ImportMarker marker{};
  if (!beginImport(context.snapshot, context.expectedPrimary, context.expectedSize, bytes.get(), size, marker)) {
    context.result = ApplyResult::WriteUncertain;
    return true;
  }
  if (!settingsrecovery::writeSettings(bytes.get(), size)) {
    context.result = ApplyResult::WriteUncertain;
    return true;
  }
  if (!finishImport(marker)) {
    context.result = ApplyResult::WriteUncertain;
    return true;
  }
  context.result = ApplyResult::Published;
  return true;
}
}  // namespace

ApplyResult publishControls(JsonDocument& proposed, bool replaceExisting, bool currentQuickTrigger,
                            const char* expectedPrimary, size_t expectedSize, char (&snapshot)[SNAPSHOT_PATH_SIZE]) {
  snapshot[0] = '\0';
  if (!proposed.is<JsonObject>() || !expectedPrimary || !expectedSize || expectedSize > MAX_BYTES)
    return ApplyResult::Unavailable;
  Context context{proposed,        ImportKind::Controls, replaceExisting, currentQuickTrigger,
                  expectedPrimary, expectedSize,         snapshot};
  return Storage.withStorageLock(publishLocked, &context) ? context.result : ApplyResult::Unavailable;
}
ApplyResult publishReaderBars(JsonDocument& proposed, const char* expectedPrimary, size_t expectedSize,
                              char (&snapshot)[SNAPSHOT_PATH_SIZE]) {
  snapshot[0] = '\0';
  if (!proposed.is<JsonObject>() || !expectedPrimary || !expectedSize || expectedSize > MAX_BYTES)
    return ApplyResult::Unavailable;
  Context context{proposed, ImportKind::ReaderBars, true, false, expectedPrimary, expectedSize, snapshot};
  return Storage.withStorageLock(publishLocked, &context) ? context.result : ApplyResult::Unavailable;
}
ApplyResult publishLibrary(JsonDocument& proposed, const char* expectedPrimary, size_t expectedSize,
                           char (&snapshot)[SNAPSHOT_PATH_SIZE]) {
  snapshot[0] = '\0';
  if (!proposed.is<JsonObject>() || !expectedPrimary || !expectedSize || expectedSize > MAX_BYTES)
    return ApplyResult::Unavailable;
  Context context{proposed, ImportKind::Library, true, false, expectedPrimary, expectedSize, snapshot};
  return Storage.withStorageLock(publishLocked, &context) ? context.result : ApplyResult::Unavailable;
}
ApplyResult publishPreferences(JsonDocument& proposed, const char* expectedPrimary, size_t expectedSize,
                               char (&snapshot)[SNAPSHOT_PATH_SIZE]) {
  snapshot[0] = '\0';
  if (!proposed.is<JsonObject>() || !expectedPrimary || !expectedSize || expectedSize > MAX_BYTES)
    return ApplyResult::Unavailable;
  Context context{proposed, ImportKind::Preferences, true, false, expectedPrimary, expectedSize, snapshot};
  return Storage.withStorageLock(publishLocked, &context) ? context.result : ApplyResult::Unavailable;
}
}  // namespace crossinkimport
