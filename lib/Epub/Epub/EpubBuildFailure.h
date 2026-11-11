#pragma once

#include <cstdint>

// Runtime diagnostics; these values are not persisted.
enum class EpubBuildFailure : uint8_t {
  None,
  OutOfMemory,
  InputOutput,
  InvalidInput,
  Cancelled,
  InvalidState,
  Unknown
};
