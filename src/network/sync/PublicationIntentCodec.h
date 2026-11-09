#pragma once

#include <CredentialIntegrity.h>
#include <strings.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace wirecheck::publication {
// XSP1 matches the dormant earlier fork's on-card intent format. Decoding a
// record does not authorize a filesystem operation or authenticate its source.
inline constexpr size_t MAX_DESTINATION = 1024;
inline constexpr size_t STAGE_LENGTH = 45;
inline constexpr size_t HEADER_LENGTH = 4 + 8 + 2 + STAGE_LENGTH + 32;
inline constexpr size_t MAX_RECORD = HEADER_LENGTH + MAX_DESTINATION + 4;
inline constexpr uint64_t MAX_SIZE = 0x7fffffffULL - 1024;

struct View {
  std::string_view stage, destination;
  uint64_t size;
  std::array<uint8_t, 32> sha256;
};
// Callers allocate this approximately 1.1 KiB value off a small task stack.
struct Intent {
  std::array<char, STAGE_LENGTH + 1> stage{};
  std::array<char, MAX_DESTINATION + 1> destination{};
  uint64_t size = 0;
  std::array<uint8_t, 32> sha256{};
};

inline bool visibleNameV1(std::string_view name) {
  if (name.empty() || name.front() == '.') return false;
  // Freeze the original V1 policy even if the current hidden-item list changes.
  constexpr const char* hiddenV1[] = {"System Volume Information", "XTCache"};
  for (const char* hidden : hiddenV1)
    if (name.size() == std::strlen(hidden) && strncasecmp(name.data(), hidden, name.size()) == 0) return false;
  return true;
}
inline bool visiblePathV1(std::string_view path) {
  if (path.empty() || path.size() > 4096 || path.front() != '/') return false;
  if (path == "/") return true;
  for (size_t start = 1; start < path.size();) {
    const size_t slash = path.find('/', start);
    const size_t end = slash == path.npos ? path.size() : slash;
    const auto part = path.substr(start, end - start);
    if (!visibleNameV1(part) || part.front() == ' ' || part.back() == ' ' || part.back() == '.') return false;
    for (const unsigned char ch : part)
      if (ch < 32 || ch == '\\' || ch == 127) return false;
    if (slash == path.npos) return true;
    start = slash + 1;
  }
  return false;
}
inline bool stageNameV1(std::string_view stage) {
  if (stage.size() != STAGE_LENGTH || !stage.starts_with("__xsync_") || !stage.ends_with(".part")) return false;
  for (const char c : stage.substr(8, 32))
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
inline bool epubV1(std::string_view path) {
  if (path.size() < 5) return false;
  constexpr char suffix[] = ".epub";
  for (size_t i = 0; i < 5; ++i) {
    char c = path[path.size() - 5 + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}
inline bool validV1(const View& view) {
  return stageNameV1(view.stage) && view.size <= MAX_SIZE && view.destination.size() <= MAX_DESTINATION &&
         view.destination.size() > 1 && visiblePathV1(view.destination) && epubV1(view.destination) &&
         std::count(view.destination.begin(), view.destination.end(), '/') <= 16;
}
inline void putLe(uint8_t* target, uint64_t value, size_t count) {
  for (size_t i = 0; i < count; ++i) target[i] = static_cast<uint8_t>(value >> (8 * i));
}
inline uint64_t getLe(const uint8_t* source, size_t count) {
  uint64_t value = 0;
  for (size_t i = 0; i < count; ++i) value |= uint64_t(source[i]) << (8 * i);
  return value;
}
// View strings remain valid for the call. Neither the View object nor its
// string storage may overlap out.
inline size_t encodeV1(const View& view, uint8_t* out, size_t capacity) {
  if (!out || !validV1(view) || capacity < HEADER_LENGTH + view.destination.size() + 4) return 0;
  std::memcpy(out, "XSP1", 4);
  putLe(out + 4, view.size, 8);
  putLe(out + 12, view.destination.size(), 2);
  std::memcpy(out + 14, view.stage.data(), STAGE_LENGTH);
  std::memcpy(out + 14 + STAGE_LENGTH, view.sha256.data(), view.sha256.size());
  std::memcpy(out + HEADER_LENGTH, view.destination.data(), view.destination.size());
  const size_t length = HEADER_LENGTH + view.destination.size();
  putLe(out + length, credential_integrity::crc32(std::string_view(reinterpret_cast<const char*>(out), length)), 4);
  return length + 4;
}
// data and out must not overlap. Invalid input leaves out unchanged.
inline bool decodeV1(const uint8_t* data, size_t length, Intent& out) {
  if (!data || length < HEADER_LENGTH + 5 || length > MAX_RECORD || std::memcmp(data, "XSP1", 4) != 0) return false;
  const size_t destinationLength = getLe(data + 12, 2);
  if (!destinationLength || destinationLength > MAX_DESTINATION || length != HEADER_LENGTH + destinationLength + 4)
    return false;
  const size_t contentLength = length - 4;
  if (credential_integrity::crc32(std::string_view(reinterpret_cast<const char*>(data), contentLength)) !=
      getLe(data + contentLength, 4))
    return false;
  View view{std::string_view(reinterpret_cast<const char*>(data + 14), STAGE_LENGTH),
            std::string_view(reinterpret_cast<const char*>(data + HEADER_LENGTH), destinationLength),
            getLe(data + 4, 8),
            {}};
  if (!validV1(view)) return false;
  std::memcpy(out.stage.data(), view.stage.data(), view.stage.size());
  out.stage[view.stage.size()] = 0;
  std::memcpy(out.destination.data(), view.destination.data(), view.destination.size());
  out.destination[view.destination.size()] = 0;
  out.size = view.size;
  std::memcpy(out.sha256.data(), data + 14 + STAGE_LENGTH, out.sha256.size());
  return true;
}
}  // namespace wirecheck::publication
