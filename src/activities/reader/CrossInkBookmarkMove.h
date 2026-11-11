#pragma once

#include <cstdint>
#include <string_view>

class HalFile;
namespace readingstats::completionmove {
struct BookmarkRewrite {
  uint64_t lengthOffset = 0;
  uint64_t suffixOffset = 0;
  uint64_t outputSize = 0;
};
enum class BookmarkRewriteResult { Ready, Invalid, Unavailable };
// Validates a CrossInk v2-v5 bookmark file and identifies its embedded book
// path. No writes occur. The caller must recheck file identity when copying.
BookmarkRewriteResult inspectBookmarkRewrite(HalFile& file, std::string_view oldPath, std::string_view newPath,
                                             BookmarkRewrite& output);
// Checks whether a file contains a prefix of the exact rewritten bytes.
// A full match sets complete; outputs change only on Ready.
BookmarkRewriteResult compareBookmarkRewrite(HalFile& input, HalFile& candidate, std::string_view oldPath,
                                             std::string_view newPath, bool& complete);
// Streams a format-preserving rewrite to a read/write staging file. Any
// existing prefix is verified first. Caller owns sync, close and publication.
BookmarkRewriteResult writeBookmarkRewrite(HalFile& input, HalFile& staging, std::string_view oldPath,
                                           std::string_view newPath);
// Writes or checks a format-preserving removal in caller-owned staging. This
// does not publish, sync, or delete either file. The caller must establish
// independent input/staging allocation chains before writing.
BookmarkRewriteResult writeBookmarkRemoval(HalFile& input, HalFile& staging, std::string_view bookPath, uint16_t first,
                                           uint16_t count);
BookmarkRewriteResult compareBookmarkRemoval(HalFile& input, HalFile& candidate, std::string_view bookPath,
                                             uint16_t first, uint16_t count, bool& complete);
}  // namespace readingstats::completionmove
