#pragma once

#include <mbedtls/sha256.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "FirmwareBoardTag.h"

namespace personalupdate {
enum class ImageResult : uint8_t {
  Pending,
  Verified,
  InvalidMetadata,
  WrongDevice,
  InvalidHeader,
  TooLong,
  TooShort,
  DigestMismatch,
  HashError
};

class ImageVerifier final {
  mbedtls_sha256_context hash;
  std::array<uint8_t, 32> expectedDigest;
  size_t expectedSize, receivedBytes = 0, headerBytes = 0;
  uint16_t expectedChip;
  std::array<uint8_t, 14> imageHeader{};
  board_tag::Scanner board;
  ImageResult result_ = ImageResult::Pending;

 public:
  ImageVerifier(size_t size, const std::array<uint8_t, 32>& digest, uint16_t chip);
  ~ImageVerifier();
  ImageVerifier(const ImageVerifier&) = delete;
  ImageVerifier& operator=(const ImageVerifier&) = delete;
  bool feed(const uint8_t* data, size_t length);
  ImageResult finish();
  ImageResult result() const { return result_; }
  size_t received() const { return receivedBytes; }
  std::span<const uint8_t> header() const { return {imageHeader.data(), headerBytes}; }
};
}  // namespace personalupdate
