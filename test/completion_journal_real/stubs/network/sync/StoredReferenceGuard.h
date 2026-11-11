#pragma once
#include <HalStorage.h>

#include <string_view>
namespace syncpublish {
enum class ReferenceGuardResult { Clear, Conflict, Unavailable };
inline bool referenceMayReferTo(std::string_view reference, std::string_view source) { return reference == source; }
inline ReferenceGuardResult inspectStoredSourceReferences(HalStorage&, std::string_view, bool (*tick)(void*),
                                                          void* ctx) {
  return tick(ctx) ? ReferenceGuardResult::Clear : ReferenceGuardResult::Unavailable;
}
}  // namespace syncpublish
