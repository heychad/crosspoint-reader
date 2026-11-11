#pragma once

#include <cstddef>
#include <string>

// Saved reading percentage, or -1 when unavailable. Never builds reading caches.
int loadBookProgress(const std::string& path);
// Optionally returns the spine-level EPUB chapter title; output may be empty.
int loadBookProgress(const std::string& path, char* chapter, size_t capacity);
