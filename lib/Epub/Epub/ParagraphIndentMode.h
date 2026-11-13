#pragma once

#include <cstdint>

// Stored IDs: keep existing fixed-space layouts as the default.
enum class ParagraphIndentMode : uint8_t { FixedSpaces = 0, Book = 1, Force = 2, Count };
