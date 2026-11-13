#pragma once
#include <mbedtls/sha256.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

inline std::vector<uint8_t> imageFixture(const char* tag = "CROSSPOINT-BOARD-V1:x4pro;") {
  std::vector<uint8_t> image(128, 0xa5);
  image[0] = 0xe9;
  image[12] = 9;
  image[13] = 0;
  std::memcpy(image.data() + 40, tag, std::strlen(tag));
  return image;
}
inline std::array<uint8_t, 32> imageDigest(const std::vector<uint8_t>& image) {
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  std::array<uint8_t, 32> result{};
  mbedtls_sha256_starts(&context, 0);
  mbedtls_sha256_update(&context, image.data(), image.size());
  mbedtls_sha256_finish(&context, result.data());
  mbedtls_sha256_free(&context);
  return result;
}
inline std::string digestText(const std::array<uint8_t, 32>& digest) {
  std::string result = "sha256:";
  constexpr char digits[] = "0123456789abcdef";
  for (auto byte : digest) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}
