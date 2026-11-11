#include "BookReaderPreferences.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <PersistableStore.h>
#include <ZipFile.h>
#include <strings.h>

#include "ReadingStatsStore.h"
#include "personalization/CrossInkBookSettings.h"

namespace {
constexpr size_t MAX_DOCUMENT_BYTES = 4096;
constexpr const char* FILE_NAME = "/reader_prefs.json";
constexpr const char* LEGACY_FILE_NAME = "/reader_settings.bin";

enum class LegacyLoad { Missing, Loaded, Failed };
struct LegacyContext {
  const std::string& path;
  BookReaderSettings& settings;
  LegacyLoad result = LegacyLoad::Failed;
};
bool readLegacyLocked(void* raw) {
  auto& context = *static_cast<LegacyContext*>(raw);
  const auto status = Storage.lookupPath(context.path.c_str());
  if (status == HalStorage::PathStatus::Failed) return false;
  // Recovery artifacts can contain newer or aliased state. Preserve all of it.
  for (const char* suffix : {".tmp", ".bak"}) {
    if (Storage.lookupPath((context.path + suffix).c_str()) != HalStorage::PathStatus::Missing) return false;
  }
  if (status == HalStorage::PathStatus::Missing) {
    context.result = LegacyLoad::Missing;
    return true;
  }
  HalFile file;
  if (!Storage.openFileForRead("PREFS", context.path, file)) return false;
  auto bytes = makeUniqueNoThrow<uint8_t[]>(256);
  if (!bytes) {
    LOG_ERR("PREFS", "Could not allocate 256 bytes for CrossInk settings");
    file.close();
    return false;
  }
  const size_t size = file.fileSize();
  const bool read = size > 0 && size <= 256 && file.read(bytes.get(), size) == static_cast<int>(size);
  const bool closed = file.close();
  crossink::BookSettingsPreview preview;
  if (!read || !closed || !crossink::decodeBookSettings(bytes.get(), size, preview) || (preview.extraFlags & 0x10))
    return false;  // CrossInk Safe Mode changes effective settings beyond the saved snapshot.
  static_assert(ReaderPreferences::FIELD_COUNT == 15);
  // Font resolution can snap an unsupported size during open. Until the
  // original font is migrated too, leave the size with the legacy record.
  static_assert(ReaderPreferences::INDEXING_BIT == (1U << 17));
  context.settings.mask = preview.nativeMask & ~1U;
  context.settings.preferences.indexingMethod = preview.indexingMethod;
  static_assert(ReaderPreferences::VERTICAL_MARGIN_BIT == (1U << 18));
  context.settings.preferences.verticalMargin = preview.verticalMargin;
  for (size_t i = 0; i < ReaderPreferences::FIELD_COUNT; ++i)
    context.settings.preferences.values[i] = preview.values[i];
  context.result = LegacyLoad::Loaded;
  return true;
}
LegacyLoad readLegacy(const std::string& path, BookReaderSettings& settings) {
  LegacyContext context{path, settings};
  if (!Storage.withStorageLock(readLegacyLocked, &context)) return LegacyLoad::Failed;
  return context.result;
}

bool existingDocument(const std::string& path, std::string& found) {
  for (const char* suffix : {"", ".bak", ".tmp"}) {
    const auto candidate = path + suffix;
    const auto status = Storage.lookupPath(candidate.c_str());
    if (status == HalStorage::PathStatus::Failed) return false;
    if (status == HalStorage::PathStatus::Present) {
      found = candidate;
      return true;
    }
  }
  found.clear();
  return true;
}

bool readLegacyDictionary(const std::string& cachePath, const std::string& bookPath, BookReaderSettings& settings,
                          bool& found) {
  found = false;
  const std::string oldCache =
      "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(bookPath.data(), bookPath.size()));
  for (const auto& cache : {cachePath, oldCache}) {
    const auto path = cache + "/dictionary.bin";
    const auto status = Storage.lookupPath(path.c_str());
    if (status == HalStorage::PathStatus::Failed) return false;
    if (status == HalStorage::PathStatus::Missing) continue;
    HalFile file;
    if (!Storage.openFileForRead("PREFS", path, file)) return false;
    const auto size = file.fileSize();
    char value[128]{};
    if (size >= sizeof(value)) return false;
    const bool read = file.read(value, size) == static_cast<int>(size);
    if (!file.close() || !read || strlen(value) != size) return false;
    found = true;
    if (!size) return true;  // CrossInk's empty file means inherit global.
    std::string name = value;
    const char* prefixes[] = {"/dictionaries/", "/.dictionaries/"};
    bool recognized = false;
    for (const auto* prefix : prefixes) {
      const size_t length = strlen(prefix);
      if (strncasecmp(name.c_str(), prefix, length) == 0) {
        name.erase(0, length);
        recognized = true;
        break;
      }
    }
    if (!name.empty() && name.back() == '/') name.pop_back();
    // CrossInk stores root/folder/index-stem; native settings store the folder.
    const auto separator = name.find('/');
    if (separator != std::string::npos) {
      const auto stem = name.substr(separator + 1);
      if (stem.empty() || stem.front() == '.' || stem.find_first_of("/\\") != std::string::npos) return false;
      name.resize(separator);
    }
    if (!recognized || name.empty() || name.size() >= sizeof(settings.preferences.dictionary) || name.front() == '.' ||
        name.find_first_of("/\\") != std::string::npos)
      return false;
    // The native resolver prefers the visible root. Do not translate a choice
    // when a same-named folder in the other root could select different data.
    const char* otherRoot = strncasecmp(value, "/.dictionaries/", 15) == 0 ? "/dictionaries/" : "/.dictionaries/";
    if (Storage.lookupPath((std::string(otherRoot) + name).c_str()) != HalStorage::PathStatus::Missing) return false;
    strcpy(settings.preferences.dictionary, name.c_str());
    settings.mask |= ReaderPreferences::DICTIONARY_BIT;
    return true;
  }
  return true;
}

bool readPreferences(const std::string& path, JsonDocument& document, BookReaderSettings& settings) {
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), document, MAX_DOCUMENT_BYTES) ||
      !document["version"].is<unsigned>() || document["version"].as<unsigned>() != 1 ||
      !document["overrides"].is<JsonObjectConst>())
    return false;
  return settings.preferences.read(document["overrides"].as<JsonObjectConst>(), settings.mask);
}
}  // namespace

bool BookReaderPreferences::load(const std::string& cachePath, const std::string& bookPath,
                                 BookReaderSettings& settings) {
  settings = {};
  legacyLoaded = false;
  legacySettings = {};
  destination = readingstats::bookDirectory(cachePath) + FILE_NAME;
  source.clear();
  writable = existingDocument(destination, source);
  if (writable && source.empty()) writable = existingDocument(cachePath + FILE_NAME, source);
  if (source.empty()) {
    const std::string legacy =
        "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(bookPath.data(), bookPath.size()));
    if (writable) writable = existingDocument(readingstats::bookDirectory(legacy) + FILE_NAME, source);
    if (writable && source.empty()) writable = existingDocument(legacy + FILE_NAME, source);
  }
  if (!writable) return false;
  if (source.empty()) {
    auto legacy = readLegacy(cachePath + LEGACY_FILE_NAME, settings);
    if (legacy == LegacyLoad::Missing) {
      const std::string oldCache =
          "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(bookPath.data(), bookPath.size()));
      if (oldCache != cachePath) legacy = readLegacy(oldCache + LEGACY_FILE_NAME, settings);
    }
    writable = legacy != LegacyLoad::Failed;
    legacyLoaded = legacy == LegacyLoad::Loaded;
    bool dictionaryFound = false;
    if (writable) writable = readLegacyDictionary(cachePath, bookPath, settings, dictionaryFound);
    legacyLoaded |= dictionaryFound;
    if (legacyLoaded) legacySettings = settings;
    if (!writable) settings = {};
    return writable;
  }
  JsonDocument document;
  writable = readPreferences(source, document, settings);
  if (writable && document["overrides"]["dictionaryName"].isUnbound()) {
    bool dictionaryFound = false;
    writable = readLegacyDictionary(cachePath, bookPath, settings, dictionaryFound);
    legacyLoaded = dictionaryFound;
    if (legacyLoaded) legacySettings = settings;
  }
  if (!writable) {
    settings = {};
    LOG_ERR("PREFS", "Keeping unreadable or unsupported book preferences at %s", source.c_str());
  }
  return writable;
}

bool BookReaderPreferences::save(const BookReaderSettings& settings) {
  if (!writable || destination.empty()) return false;
  JsonDocument document;
  std::string current;
  if (!existingDocument(destination, current)) return false;
  if (!current.empty()) source = current;
  if (legacyLoaded && source.empty() && settings.mask == legacySettings.mask &&
      (settings.preferences.difference(legacySettings.preferences) & settings.mask) == 0)
    return true;
  if (!source.empty()) {
    BookReaderSettings previous;
    if (!readPreferences(source, document, previous)) {
      writable = false;
      LOG_ERR("PREFS", "Keeping changed or unreadable book preferences at %s", source.c_str());
      return false;
    }
  } else if (settings.mask == 0 && !legacyLoaded) {
    return true;
  }
  // A recovered temp may be the only valid copy. Preserve it before reusing staging.
  if (source == destination + ".tmp") {
    if (!Storage.rename(source.c_str(), destination.c_str())) return false;
    source = destination;
  }
  document["version"] = 1;
  auto overrides = document["overrides"].is<JsonObject>() ? document["overrides"].as<JsonObject>()
                                                          : document["overrides"].to<JsonObject>();
  settings.preferences.write(overrides, settings.mask);
  const auto directory = destination.substr(0, destination.find_last_of('/'));
  if (!Storage.mkdir(directory.c_str()) && !Storage.exists(directory.c_str())) return false;
  if (!PersistableStoreBase::writeDocToFileAtomic(destination.c_str(), document, MAX_DOCUMENT_BYTES)) {
    LOG_ERR("PREFS", "Could not save book preferences at %s", destination.c_str());
    return false;
  }
  source = destination;
  legacyLoaded = false;
  return true;
}

bool BookReaderPreferences::prepareMove(const std::string& cachePath, const std::string& bookPath) {
  BookReaderPreferences store;
  BookReaderSettings settings;
  if (!store.load(cachePath, bookPath, settings)) return false;
  // A move would strand unsupported CrossInk settings at the original path.
  if (store.legacyLoaded) return false;
  const auto legacyCache = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(bookPath.data(), bookPath.size()));
  for (const auto& candidate : {cachePath + LEGACY_FILE_NAME, legacyCache + LEGACY_FILE_NAME}) {
    for (const char* suffix : {"", ".tmp", ".bak"}) {
      if (Storage.lookupPath((candidate + suffix).c_str()) != HalStorage::PathStatus::Missing) return false;
    }
  }
  if (store.source.empty()) return true;
  if (!store.save(settings)) return false;
  JsonDocument document;
  if (!readPreferences(store.destination, document, settings)) return false;
  // Keep a second copy with the original book if the surrounding move cannot roll back.
  return PersistableStoreBase::writeDocToFileAtomic((cachePath + FILE_NAME).c_str(), document, MAX_DOCUMENT_BYTES);
}
