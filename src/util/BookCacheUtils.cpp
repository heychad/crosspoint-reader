#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Xtc.h>

#include "ClippingStore.h"
#include "activities/reader/ReadingStatsStore.h"
#include "network/ProtectedPaths.h"

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
#include <esp_random.h>
#include <strings.h>

#include <string_view>

#include "network/FileChecksum.h"
#endif

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasReflowableBookExtension(path)) {
    const Epub book(path, "/.crosspoint");
    if (!readingstats::clearCache(book.getCachePath())) LOG_ERR("BookCache", "Cache invalidation failed");
  } else if (FsHelpers::hasXtcExtension(path)) {
    const Xtc book(path, "/.crosspoint");
    if (!readingstats::clearCache(book.getCachePath())) LOG_ERR("BookCache", "Cache invalidation failed");
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}

bool removeBookFile(const std::string& path) {
  if (protectedpaths::isBookDeletionPath(path.c_str())) return false;
  if (!Storage.remove(path.c_str())) return false;
  clearBookCache(path);
  return ClippingStore::deleteForFilePath(path, "epub");
}

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
namespace {
using protectedpaths::BOOK_DELETION_QUEUE;
constexpr std::string_view HELD_SUFFIX = ".held";
constexpr char COMMITTED[] = "/.committed";

// A marker is written only after the FAT rename finishes. An interrupted rename
// can leave aliases sharing clusters, even if the original alias moves later.
bool checkDeletionReceipt(const std::string& held, const std::string& previous, bool acknowledge) {
  if (!protectedpaths::isBookDeletionPath(held.c_str()) || !std::string_view(held).ends_with(HELD_SUFFIX) ||
      !filechecksum::validPath(held) || (!previous.empty() && !filechecksum::validPath(previous))) {
    return false;
  }
  constexpr size_t identifierStart = BOOK_DELETION_QUEUE.size() + 1;
  const auto firstSlash = held.find('/', identifierStart);
  if (firstSlash != identifierStart + 32 || held.find_first_not_of("0123456789abcdef", identifierStart) != firstSlash) {
    return false;
  }
  const auto original = held.substr(firstSlash, held.size() - firstSlash - HELD_SUFFIX.size());
  if (!filechecksum::validPath(original) || !FsHelpers::hasEpubExtension(original)) return false;
  const std::string event = held.substr(0, firstSlash);
  const std::string marker = event + COMMITTED;
  struct Receipt {
    const std::string& held;
    const std::string& original;
    const std::string& previous;
    const std::string& event;
    const std::string& marker;
    bool acknowledge;
  } receipt{held, original, previous, event, marker, acknowledge};
  return Storage.withStorageLock(
      [](void* context) {
        const auto& r = *static_cast<const Receipt*>(context);
        using PathStatus = HalStorage::PathStatus;
        if (Storage.lookupPath(r.marker.c_str()) != PathStatus::Present ||
            Storage.lookupPath(r.held.c_str()) != PathStatus::Present ||
            Storage.lookupPath(r.original.c_str()) != PathStatus::Missing ||
            (!r.previous.empty() && Storage.lookupPath(r.previous.c_str()) != PathStatus::Missing)) {
          return false;
        }
        if (!r.acknowledge) return true;
        if (!Storage.remove(r.held.c_str())) return false;
        std::string parent = r.held.substr(0, r.held.find_last_of('/'));
        while (parent != r.event && Storage.rmdir(parent.c_str())) {
          parent.resize(parent.find_last_of('/'));
        }
        if (Storage.rmdir(r.marker.c_str())) Storage.rmdir(r.event.c_str());
        return true;
      },
      &receipt);
}
}  // namespace
#endif

bool bookDeletionReceiptReady(const std::string& held, const std::string& previous) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  return checkDeletionReceipt(held, previous, false);
#else
  return false;
#endif
}

bool acknowledgeBookDeletion(const std::string& held) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  return checkDeletionReceipt(held, {}, true);
#else
  return false;
#endif
}

bool removeBookFileWithReceipt(const std::string& path) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (protectedpaths::isBookDeletionPath(path.c_str())) return false;
  const auto nameLength = path.size() - path.find_last_of('/') - 1;
  if (nameLength <= 230 && FsHelpers::hasEpubExtension(path) && strncasecmp(path.c_str(), "/zDev/", 6) != 0 &&
      filechecksum::validPath(path)) {
    auto source = Storage.open(path.c_str());
    if (!source || source.isDirectory()) return false;
    const bool empty = source.size() == 0;
    source.close();
    if (empty) return removeBookFile(path);
    // Nest the old path below a random directory. The held suffix keeps the
    // EPUB out of library scans. A same-card rename stages its bytes.
    uint8_t nonce[16];
    esp_fill_random(nonce, sizeof(nonce));
    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    char identifier[33];
    for (size_t i = 0; i < sizeof(nonce); ++i) {
      identifier[i * 2] = HEX_DIGITS[nonce[i] >> 4];
      identifier[i * 2 + 1] = HEX_DIGITS[nonce[i] & 15];
    }
    identifier[32] = '\0';
    const std::string event = std::string(BOOK_DELETION_QUEUE) + "/" + identifier;
    const std::string held = event + path + std::string(HELD_SUFFIX);
    if (!filechecksum::validPath(held)) return removeBookFile(path);
    if (Storage.exists(event.c_str())) return false;
    const std::string parent = held.substr(0, held.find_last_of('/'));
    if (!Storage.mkdir(parent.c_str(), true) || Storage.exists(held.c_str()) ||
        !Storage.rename(path.c_str(), held.c_str())) {
      return false;
    }
    if (!Storage.mkdir((event + COMMITTED).c_str())) {
      LOG_ERR("BookCache", "Could not seal deletion receipt: %s", held.c_str());
      return false;
    }
    clearBookCache(path);
    return ClippingStore::deleteForFilePath(path, "epub");
  }
#endif
  return removeBookFile(path);
}
