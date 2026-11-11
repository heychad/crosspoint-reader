#include "CrossInkBookmarkMove.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstring>

namespace readingstats::completionmove {
namespace {
constexpr uint64_t MAX_FILE_BYTES = 256 * 1024;
constexpr uint32_t MAX_STRING_BYTES = 4096;
constexpr uint16_t MAX_BOOKMARKS = 1024;
bool readExact(HalFile& file, uint8_t* bytes, size_t size) { return file.read(bytes, size) == static_cast<int>(size); }
uint32_t little32(const uint8_t* bytes) {
  return uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) | (uint32_t{bytes[2]} << 16) | (uint32_t{bytes[3]} << 24);
}
}  // namespace

BookmarkRewriteResult inspectBookmarkRewrite(HalFile& file, std::string_view oldPath, std::string_view newPath,
                                             BookmarkRewrite& output) {
  if (!file || file.isDirectory() || oldPath.empty() || newPath.empty() || oldPath.size() > MAX_STRING_BYTES ||
      newPath.size() > MAX_STRING_BYTES || oldPath.find('\0') != std::string_view::npos ||
      newPath.find('\0') != std::string_view::npos)
    return BookmarkRewriteResult::Invalid;
  const uint64_t size = file.fileSize();
  if (size > MAX_FILE_BYTES || size < 2) return BookmarkRewriteResult::Invalid;
  if (!file.seekSet(0)) return BookmarkRewriteResult::Unavailable;
  uint8_t head[3]{};
  if (!readExact(file, head, 2)) return BookmarkRewriteResult::Unavailable;
  const uint8_t version = head[0];
  if (version < 2 || version > 5) return BookmarkRewriteResult::Invalid;
  uint16_t count = head[1];
  uint64_t offset = 2;
  if (version >= 3) {
    if (size < 3) return BookmarkRewriteResult::Invalid;
    if (!readExact(file, head + 2, 1)) return BookmarkRewriteResult::Unavailable;
    count |= static_cast<uint16_t>(head[2]) << 8;
    ++offset;
  }
  if (count > MAX_BOOKMARKS) return BookmarkRewriteResult::Invalid;
  BookmarkRewrite candidate;
  for (unsigned field = 0; field < 3; ++field) {
    if (offset + 4 > size) return BookmarkRewriteResult::Invalid;
    uint8_t encodedLength[4];
    if (!readExact(file, encodedLength, sizeof(encodedLength))) return BookmarkRewriteResult::Unavailable;
    const uint32_t length = little32(encodedLength);
    if (length > MAX_STRING_BYTES || offset + 4 + length > size) return BookmarkRewriteResult::Invalid;
    if (field == 2) {
      candidate.lengthOffset = offset;
      if (length != oldPath.size()) return BookmarkRewriteResult::Invalid;
      char buffer[128];
      for (size_t position = 0; position < length;) {
        const size_t amount = std::min(sizeof(buffer), static_cast<size_t>(length) - position);
        if (!readExact(file, reinterpret_cast<uint8_t*>(buffer), amount)) return BookmarkRewriteResult::Unavailable;
        if (std::memcmp(buffer, oldPath.data() + position, amount) != 0) return BookmarkRewriteResult::Invalid;
        position += amount;
      }
      candidate.suffixOffset = offset + 4 + length;
    } else if (!file.seekSet(static_cast<size_t>(offset + 4 + length))) {
      return BookmarkRewriteResult::Unavailable;
    }
    offset += 4 + length;
  }
  const uint64_t recordSize = 58 + (version >= 4 ? 2 : 0) + (version >= 5 ? 64 : 0);
  if (candidate.suffixOffset + recordSize * count > size) return BookmarkRewriteResult::Invalid;
  candidate.outputSize = size - oldPath.size() + newPath.size();
  if (candidate.outputSize > MAX_FILE_BYTES) return BookmarkRewriteResult::Invalid;
  output = candidate;
  return BookmarkRewriteResult::Ready;
}

namespace {
BookmarkRewriteResult transform(HalFile& input, HalFile& output, std::string_view oldPath, std::string_view newPath,
                                bool write, bool& complete) {
  if (!output || output.isDirectory()) return BookmarkRewriteResult::Invalid;
  BookmarkRewrite rewrite;
  const auto inspected = inspectBookmarkRewrite(input, oldPath, newPath, rewrite);
  if (inspected != BookmarkRewriteResult::Ready) return inspected;
  const uint64_t existing = output.fileSize();
  if (existing > rewrite.outputSize || !input.seekSet(0) || !output.seekSet(0)) return BookmarkRewriteResult::Invalid;
  uint64_t offset = 0;
  uint8_t buffer[256], actual[256];
  bool mismatch = false;
  const auto emit = [&](const uint8_t* bytes, size_t count) {
    size_t done = 0;
    while (done < count) {
      const size_t chunk = std::min(sizeof(actual), count - done);
      size_t checked = 0;
      if (offset < existing) {
        checked = static_cast<size_t>(std::min<uint64_t>(chunk, existing - offset));
        if (!readExact(output, actual, checked)) return false;
        if (std::memcmp(bytes + done, actual, checked) != 0) {
          mismatch = true;
          return false;
        }
      }
      if (write && checked < chunk && output.write(bytes + done + checked, chunk - checked) != chunk - checked)
        return false;
      offset += chunk;
      done += chunk;
    }
    return true;
  };
  const auto copy = [&](uint64_t count) {
    while (count) {
      const auto amount = static_cast<size_t>(std::min<uint64_t>(sizeof(buffer), count));
      if (!readExact(input, buffer, amount) || !emit(buffer, amount)) return false;
      count -= amount;
    }
    return true;
  };
  if (!copy(rewrite.lengthOffset))
    return mismatch ? BookmarkRewriteResult::Invalid : BookmarkRewriteResult::Unavailable;
  const auto length = static_cast<uint32_t>(newPath.size());
  const uint8_t encoded[] = {static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8),
                             static_cast<uint8_t>(length >> 16), static_cast<uint8_t>(length >> 24)};
  if (!emit(encoded, sizeof(encoded)) || !emit(reinterpret_cast<const uint8_t*>(newPath.data()), newPath.size()) ||
      !input.seekSet(static_cast<size_t>(rewrite.suffixOffset)) || !copy(input.fileSize() - rewrite.suffixOffset))
    return mismatch ? BookmarkRewriteResult::Invalid : BookmarkRewriteResult::Unavailable;
  if (write && output.fileSize() != rewrite.outputSize) return BookmarkRewriteResult::Unavailable;
  complete = existing == rewrite.outputSize;
  return BookmarkRewriteResult::Ready;
}
}  // namespace

BookmarkRewriteResult compareBookmarkRewrite(HalFile& input, HalFile& candidate, std::string_view oldPath,
                                             std::string_view newPath, bool& complete) {
  bool matched = false;
  const auto result = transform(input, candidate, oldPath, newPath, false, matched);
  if (result == BookmarkRewriteResult::Ready) complete = matched;
  return result;
}

BookmarkRewriteResult writeBookmarkRewrite(HalFile& input, HalFile& staging, std::string_view oldPath,
                                           std::string_view newPath) {
  bool ignored = false;
  return transform(input, staging, oldPath, newPath, true, ignored);
}

namespace {
BookmarkRewriteResult removeRecords(HalFile& input, HalFile& output, std::string_view bookPath, uint16_t first,
                                    uint16_t count, bool write, bool& complete) {
  BookmarkRewrite layout;
  const auto inspected = inspectBookmarkRewrite(input, bookPath, bookPath, layout);
  if (inspected != BookmarkRewriteResult::Ready) return inspected;
  if (!input.seekSet(0)) return BookmarkRewriteResult::Unavailable;
  uint8_t head[3]{};
  if (!readExact(input, head, 2)) return BookmarkRewriteResult::Unavailable;
  const size_t headerSize = head[0] == 2 ? 2 : 3;
  if (headerSize == 3 && !readExact(input, head + 2, 1)) return BookmarkRewriteResult::Unavailable;
  const uint16_t oldCount = head[1] | (headerSize == 3 ? static_cast<uint16_t>(head[2]) << 8 : 0);
  if (!count || first >= oldCount || count > oldCount - first) return BookmarkRewriteResult::Invalid;
  const uint64_t recordSize = 58 + (head[0] >= 4 ? 2 : 0) + (head[0] >= 5 ? 64 : 0);
  const uint64_t removeStart = layout.suffixOffset + recordSize * first;
  const uint64_t removeEnd = removeStart + recordSize * count;
  const uint64_t outputSize = input.fileSize() - recordSize * count;
  const uint64_t existing = output.fileSize();
  if (!output || output.isDirectory() || existing > outputSize || !output.seekSet(0))
    return BookmarkRewriteResult::Invalid;
  const uint16_t remaining = oldCount - count;
  head[1] = static_cast<uint8_t>(remaining);
  head[2] = static_cast<uint8_t>(remaining >> 8);
  uint8_t buffer[128], actual[128];
  uint64_t offset = 0;
  bool mismatch = false;
  const auto emit = [&](const uint8_t* bytes, size_t length) {
    const size_t checked = static_cast<size_t>(std::min<uint64_t>(length, existing > offset ? existing - offset : 0));
    if (checked && !readExact(output, actual, checked)) return false;
    if (checked && std::memcmp(bytes, actual, checked)) {
      mismatch = true;
      return false;
    }
    if (write && checked < length && output.write(bytes + checked, length - checked) != length - checked) return false;
    offset += length;
    return true;
  };
  const auto copy = [&](uint64_t length) {
    while (length) {
      const size_t amount = static_cast<size_t>(std::min<uint64_t>(sizeof(buffer), length));
      if (!readExact(input, buffer, amount) || !emit(buffer, amount)) return false;
      length -= amount;
    }
    return true;
  };
  if (!input.seekSet(headerSize) || !emit(head, headerSize) || !copy(removeStart - headerSize) ||
      !input.seekSet(removeEnd) || !copy(input.fileSize() - removeEnd))
    return mismatch ? BookmarkRewriteResult::Invalid : BookmarkRewriteResult::Unavailable;
  if (write && output.fileSize() != outputSize) return BookmarkRewriteResult::Unavailable;
  complete = existing == outputSize;
  return BookmarkRewriteResult::Ready;
}
}  // namespace

BookmarkRewriteResult writeBookmarkRemoval(HalFile& input, HalFile& staging, std::string_view bookPath, uint16_t first,
                                           uint16_t count) {
  bool ignored = false;
  return removeRecords(input, staging, bookPath, first, count, true, ignored);
}

BookmarkRewriteResult compareBookmarkRemoval(HalFile& input, HalFile& candidate, std::string_view bookPath,
                                             uint16_t first, uint16_t count, bool& complete) {
  bool matched = false;
  const auto result = removeRecords(input, candidate, bookPath, first, count, false, matched);
  if (result == BookmarkRewriteResult::Ready) complete = matched;
  return result;
}
}  // namespace readingstats::completionmove
