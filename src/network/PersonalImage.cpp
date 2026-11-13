#include "PersonalImage.h"

#include <algorithm>
#include <cstring>

personalupdate::ImageVerifier::ImageVerifier(const size_t size, const std::array<uint8_t, 32>& digest,
                                             const uint16_t chip)
    : expectedDigest(digest), expectedSize(size), expectedChip(chip) {
  mbedtls_sha256_init(&hash);
  if (board_tag::boardNameLen() != 5 || std::memcmp(board_tag::boardName(), "x4pro", 5) || chip == 0xffff) {
    result_ = ImageResult::WrongDevice;
  } else if (size < 24) {
    result_ = ImageResult::InvalidMetadata;
  } else if (mbedtls_sha256_starts(&hash, 0) != 0) {
    result_ = ImageResult::HashError;
  }
}

personalupdate::ImageVerifier::~ImageVerifier() { mbedtls_sha256_free(&hash); }

bool personalupdate::ImageVerifier::feed(const uint8_t* data, const size_t length) {
  if (result_ != ImageResult::Pending) {
    if (result_ == ImageResult::Verified && length) result_ = ImageResult::TooLong;
    return false;
  }
  if (length > expectedSize - receivedBytes) {
    result_ = ImageResult::TooLong;
    return false;
  }
  if (!length) return true;
  if (!data) {
    result_ = ImageResult::InvalidHeader;
    return false;
  }
  if (headerBytes < imageHeader.size()) {
    const size_t copied = std::min(length, imageHeader.size() - headerBytes);
    std::memcpy(imageHeader.data() + headerBytes, data, copied);
    headerBytes += copied;
    if (headerBytes == imageHeader.size()) {
      uint16_t chip;
      std::memcpy(&chip, imageHeader.data() + 12, sizeof(chip));
      if (imageHeader[0] != 0xe9) {
        result_ = ImageResult::InvalidHeader;
        return false;
      }
      if (chip != expectedChip) {
        result_ = ImageResult::WrongDevice;
        return false;
      }
    }
  }
  board.feed(data, length);
  if (board.mismatch()) {
    result_ = ImageResult::WrongDevice;
    return false;
  }
  if (mbedtls_sha256_update(&hash, data, length) != 0) {
    result_ = ImageResult::HashError;
    return false;
  }
  receivedBytes += length;
  return true;
}

personalupdate::ImageResult personalupdate::ImageVerifier::finish() {
  if (result_ != ImageResult::Pending) return result_;
  if (receivedBytes != expectedSize || headerBytes != imageHeader.size()) return result_ = ImageResult::TooShort;
  if (!board.matched()) return result_ = ImageResult::WrongDevice;
  std::array<uint8_t, 32> digest{};
  if (mbedtls_sha256_finish(&hash, digest.data()) != 0) return result_ = ImageResult::HashError;
  if (digest != expectedDigest) return result_ = ImageResult::DigestMismatch;
  return result_ = ImageResult::Verified;
}
