#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

// Write a native EPUB or XTC position. Failed staging leaves the current position in place.
// A power cut between removing the old file and renaming the new file can leave it missing.
bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t size);

}  // namespace ProgressFile
