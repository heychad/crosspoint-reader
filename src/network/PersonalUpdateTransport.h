#pragma once

#include <cstddef>
#include <cstdint>

namespace personalupdate {
using UpdateSink = bool (*)(void* context, const uint8_t* data, size_t size);
enum class ReleaseFetch { Success, NotFound, Failure };

// Available only when the update-specific transport authenticates peers and redirects.
bool transportAvailable();
// Synchronous calls: the sink and context are never retained after return.
ReleaseFetch fetchRelease(UpdateSink sink, void* context);
bool fetchImage(const char* url, UpdateSink sink, void* context);
}  // namespace personalupdate
