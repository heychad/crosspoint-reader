#pragma once

#include <cstddef>
#include <cstdint>

namespace personalupdate {
using UpdateSink = bool (*)(void* context, const uint8_t* data, size_t size);

// Available only when the update-specific transport authenticates peers and redirects.
bool transportAvailable();
// Synchronous calls: the sink and context are never retained after return.
bool fetchRelease(UpdateSink sink, void* context);
bool fetchImage(const char* url, UpdateSink sink, void* context);
}  // namespace personalupdate
