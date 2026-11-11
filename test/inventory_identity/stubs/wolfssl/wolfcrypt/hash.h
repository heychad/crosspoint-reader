#pragma once
#include <cstdint>
#include <vector>

namespace fake {
inline bool failHash = false;
inline std::vector<uint8_t> hashInput;
}  // namespace fake
inline int wc_Sha256Hash(const uint8_t* input, unsigned size, uint8_t* output) {
  if (fake::failHash) return -1;
  fake::hashInput.assign(input, input + size);
  for (unsigned i = 0; i < 32; ++i) output[i] = static_cast<uint8_t>(i ^ input[i % size] ^ input[size - 1]);
  return 0;
}
