#pragma once

#include <cstdint>
#include <string>

namespace readingstats::completionmove {
struct FileIdentity {
  bool present = false;
  uint64_t size = 0;
  uint64_t checksum = 0;
};
enum class FileResult { Ready, Moved, AlreadyMoved, Copied, AlreadyCopied, Absent, Conflict, Unavailable, Uncertain };
enum class MoveFileState { AtSource, AtDestination, Absent, Duplicate, Conflict, Unavailable };

// The identity must be durably recorded before the first rename. These APIs
// do not publish the identity and cannot be used as a move transaction alone.
FileResult captureFile(const std::string& path, FileIdentity& identity);
// Read-only recovery classification. Duplicate paths may share disk clusters;
// this result never authorizes deleting either entry.
MoveFileState inspectMoveFile(const std::string& from, const std::string& to, const FileIdentity& identity);
FileResult moveFileChecked(const std::string& from, const std::string& to, const FileIdentity& identity);
// Copies via an exclusive staging file, retaining the original. A complete
// staging file can be resumed after its bytes are checked against the source;
// conflicting staging files are preserved.
FileResult copyFileChecked(const std::string& from, const std::string& to, const FileIdentity& identity);
}  // namespace readingstats::completionmove
