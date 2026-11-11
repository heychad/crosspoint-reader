#pragma once

#include <ArduinoJson.h>

namespace crossinkimport {
inline constexpr char SOURCE_PATH[] = "/.crosspoint/crossink-settings.json";
inline constexpr char BACKUP_PATH[] = "/.crosspoint/crossink-settings.json.bak";
inline constexpr char TEMP_PATH[] = "/.crosspoint/crossink-settings.json.tmp";
inline constexpr size_t MAX_SOURCE_BYTES = 32768;

enum class SourceResult { Loaded, Missing, RecoveryPresent, Invalid, Unavailable };
// Reads only the CrossInk-specific primary. Caller-owned output changes only
// after a complete successful read and parse.
SourceResult readSource(JsonDocument& output);
}  // namespace crossinkimport
