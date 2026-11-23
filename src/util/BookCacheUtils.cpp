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

#include "BookDeletionPath.h"
#include "BookMoveStorage.h"
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
bool removeEmptyDirectory(const std::string& path) {
  auto directory = Storage.openChecked(path.c_str());
  if (!directory) return false;
  bool empty = directory.isDirectory();
  for (unsigned i = 0; empty && i < 3; ++i) {
    auto entry = directory.openNextFile();
    if (!entry) {
      const bool cleanEnd = !directory.hasError();
      const bool closed = directory.close();
      return cleanEnd && closed && Storage.observedSdmmcHealthy() && Storage.rmdir(path.c_str());
    }
    char name[3]{};
    const auto length = entry.getName(name, sizeof(name));
    const bool closed = entry.close();
    empty = closed && ((length == 1 && std::strcmp(name, ".") == 0) || (length == 2 && std::strcmp(name, "..") == 0));
  }
  directory.close();
  return false;
}

bool checkDeletionReceipt(const std::string& held, const std::string& previous, filechecksum::Job* checksum,
                          bool acknowledge) {
  bookmove::DeletionPath deletion;
  if (!bookmove::parseDeletionPath(held, deletion) || !filechecksum::validPath(held) ||
      (!previous.empty() && !filechecksum::validPath(previous))) {
    return false;
  }
  const std::string original(deletion.original);
  if (!filechecksum::validPath(original) || !FsHelpers::hasEpubExtension(original)) return false;
  const std::string event = held.substr(0, BOOK_DELETION_QUEUE.size() + 1 + 32);
  struct Receipt {
    const std::string& held;
    const std::string& original;
    const std::string& previous;
    const std::string& event;
    filechecksum::Job* checksum;
    bool acknowledge;
  } receipt{held, original, previous, event, checksum, acknowledge};
  return Storage.withStorageLock(
      [](void* context) {
        const auto& r = *static_cast<const Receipt*>(context);
        using PathStatus = HalStorage::PathStatus;
        HalStorage::MutationSnapshot snapshot;
        const auto fingerprint = r.checksum ? r.checksum->deletionFingerprint(r.held) : std::nullopt;
        if (!Storage.beginMutationSnapshot(snapshot) || ((r.acknowledge || !r.previous.empty()) && !fingerprint) ||
            (!r.previous.empty() && Storage.lookupPath(r.previous.c_str()) != PathStatus::Missing) ||
            !bookmove::verifyCommittedDeletion(r.held, r.original, fingerprint) ||
            !Storage.mutationSnapshotUnchanged(snapshot)) {
          return false;
        }
        if (!r.acknowledge) return true;
        if (!Storage.remove(r.held.c_str())) return false;
        std::string parent = r.held.substr(0, r.held.find_last_of('/'));
        while (removeEmptyDirectory(parent) && parent != r.event) {
          parent.resize(parent.find_last_of('/'));
        }
        return true;
      },
      &receipt);
}
}  // namespace
#endif

bool bookDeletionReceiptReady(const std::string& held, const std::string& previous, filechecksum::Job* checksum) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  return checkDeletionReceipt(held, previous, checksum, false);
#else
  return false;
#endif
}

bool acknowledgeBookDeletion(const std::string& held, filechecksum::Job* checksum) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  return checkDeletionReceipt(held, {}, checksum, true);
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
    const std::string held = event + path + std::string(bookmove::HELD_SUFFIX);
    if (!filechecksum::validPath(held) || !bookmove::validMovePath(held)) return false;
    if (Storage.lookupPath(event.c_str()) != HalStorage::PathStatus::Missing) return false;
    const std::string parent = held.substr(0, held.find_last_of('/'));
    if (!Storage.mkdir(parent.c_str(), true) || bookmove::move(path, held) != bookmove::Result::Moved) return false;
    // Keep local reading state available if the library restores this EPUB.
    return true;
  }
#endif
  return removeBookFile(path);
}
