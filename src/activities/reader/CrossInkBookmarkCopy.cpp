#include <HalStorage.h>

#include "CrossInkBookmarkMove.h"

namespace readingstats::completionmove {
FileResult copyBookmarkChecked(const std::string& source, const std::string& target, std::string_view oldBook,
                               std::string_view newBook, const FileIdentity& expected) {
  FileIdentity actual, destination, partial;
  const std::string stagingPath = target + ".completion-copy";
  const auto a = captureFile(source, actual), b = captureFile(target, destination),
             c = captureFile(stagingPath, partial);
  if (a == FileResult::Unavailable || b == FileResult::Unavailable || c == FileResult::Unavailable)
    return FileResult::Unavailable;
  if ((a != FileResult::Ready && a != FileResult::Absent) || (b != FileResult::Ready && b != FileResult::Absent) ||
      (c != FileResult::Ready && c != FileResult::Absent))
    return FileResult::Conflict;
  if (!expected.present)
    return !actual.present && !destination.present && !partial.present ? FileResult::Absent : FileResult::Conflict;
  if (!actual.present || actual.size != expected.size || actual.checksum != expected.checksum)
    return FileResult::Conflict;
  if (destination.present && partial.present) return FileResult::Conflict;
  if (destination.present && !Storage.distinctMoveChains(source.c_str(), target.c_str())) return FileResult::Conflict;
  if (partial.present && !Storage.distinctMoveChains(source.c_str(), stagingPath.c_str())) return FileResult::Conflict;
  HalFile input;
  if (!Storage.openFileForRead("BKM", source, input)) return FileResult::Unavailable;
  if (destination.present) {
    HalFile output;
    if (!Storage.openFileForRead("BKM", target, output)) {
      input.close();
      return FileResult::Unavailable;
    }
    bool complete = false;
    const auto compared = compareBookmarkRewrite(input, output, oldBook, newBook, complete);
    const bool outputClosed = output.close(), inputClosed = input.close();
    if (!outputClosed || !inputClosed || compared == BookmarkRewriteResult::Unavailable) return FileResult::Unavailable;
    return compared == BookmarkRewriteResult::Ready && complete ? FileResult::AlreadyCopied : FileResult::Conflict;
  }
  HalFile output = Storage.openChecked(stagingPath.c_str(), partial.present ? O_RDWR : (O_RDWR | O_CREAT | O_EXCL));
  if (!output) {
    input.close();
    return FileResult::Uncertain;
  }
  const auto written = writeBookmarkRewrite(input, output, oldBook, newBook);
  const bool synced = output.sync();
  bool complete = false;
  const auto compared = written == BookmarkRewriteResult::Ready && synced
                            ? compareBookmarkRewrite(input, output, oldBook, newBook, complete)
                            : BookmarkRewriteResult::Unavailable;
  const bool outputClosed = output.close(), inputClosed = input.close();
  if (written == BookmarkRewriteResult::Invalid || compared == BookmarkRewriteResult::Invalid)
    return FileResult::Conflict;
  if (!synced || !outputClosed || !inputClosed || compared != BookmarkRewriteResult::Ready || !complete)
    return FileResult::Uncertain;
  if (!Storage.distinctMoveChains(source.c_str(), stagingPath.c_str())) return FileResult::Conflict;
  if (Storage.lookupPath(target.c_str()) != HalStorage::PathStatus::Missing ||
      !Storage.rename(stagingPath.c_str(), target.c_str()))
    return FileResult::Uncertain;
  // Re-read after publication before treating this file as complete.
  HalFile checkInput, checkTarget;
  if (!Storage.openFileForRead("BKM", source, checkInput)) return FileResult::Uncertain;
  if (!Storage.openFileForRead("BKM", target, checkTarget)) {
    checkInput.close();
    return FileResult::Uncertain;
  }
  complete = false;
  const auto verified = compareBookmarkRewrite(checkInput, checkTarget, oldBook, newBook, complete);
  const bool targetClosed = checkTarget.close(), sourceClosed = checkInput.close();
  return targetClosed && sourceClosed && verified == BookmarkRewriteResult::Ready && complete &&
                 Storage.distinctMoveChains(source.c_str(), target.c_str())
             ? FileResult::Copied
             : FileResult::Uncertain;
}

}  // namespace readingstats::completionmove
