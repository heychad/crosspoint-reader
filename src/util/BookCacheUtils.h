#pragma once

#include <string>

namespace filechecksum {
class Job;
}

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT). Does nothing for other file types.
void clearBookCache(const std::string& path);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);

// Delete a local book and its local clipping state after the file is removed.
bool removeBookFile(const std::string& path);

enum class BookDeleteIssue { None, InsufficientSpace, UnsupportedPath, UnsupportedSize };
// Localized guidance for failures the reader can correct, or nullptr.
const char* bookDeleteIssueMessage(BookDeleteIssue issue);

// Explicit file deletion may stage an EPUB for host-side deletion sync.
bool removeBookFileWithReceipt(const std::string& path, BookDeleteIssue* issue = nullptr);
// Check that a staged EPUB has a completed rename and the old path is absent.
bool bookDeletionReceiptReady(const std::string& held, const std::string& previous, filechecksum::Job* checksum);
bool acknowledgeBookDeletion(const std::string& held, filechecksum::Job* checksum);
