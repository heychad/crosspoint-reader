#pragma once
#include <cstddef>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
using mbedtls_sha256_context = CC_SHA256_CTX;
#else
#include <openssl/sha.h>
using mbedtls_sha256_context = SHA256_CTX;
#endif
namespace fakehash {
inline bool failStart = false, failUpdate = false, failFinish = false;
}
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* context, int) {
#if defined(__APPLE__)
  return !fakehash::failStart && CC_SHA256_Init(context) == 1 ? 0 : -1;
#else
  return !fakehash::failStart && SHA256_Init(context) == 1 ? 0 : -1;
#endif
}
inline int mbedtls_sha256_update(mbedtls_sha256_context* context, const unsigned char* data, size_t size) {
#if defined(__APPLE__)
  return !fakehash::failUpdate && CC_SHA256_Update(context, data, static_cast<CC_LONG>(size)) == 1 ? 0 : -1;
#else
  return !fakehash::failUpdate && SHA256_Update(context, data, size) == 1 ? 0 : -1;
#endif
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* context, unsigned char* digest) {
#if defined(__APPLE__)
  return !fakehash::failFinish && CC_SHA256_Final(digest, context) == 1 ? 0 : -1;
#else
  return !fakehash::failFinish && SHA256_Final(digest, context) == 1 ? 0 : -1;
#endif
}
