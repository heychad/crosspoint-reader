#pragma once

#include <ArduinoJson.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace settingsrecovery {
// ArduinoJson owns these allocations and releases them before the allocator.
// The limit includes each allocation's bookkeeping header.
class SettingsJsonAllocator final : public ArduinoJson::Allocator {
 public:
  static constexpr size_t LIMIT = 16384;

  void* allocate(size_t size) override {
    if (size > LIMIT - sizeof(Header) || used > LIMIT - sizeof(Header) - size) return nullptr;
    auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
    if (!header) return nullptr;
    header->size = size;
    used += sizeof(Header) + size;
    if (used > peak) peak = used;
    return header + 1;
  }

  void deallocate(void* ptr) override {
    if (!ptr) return;
    auto* header = static_cast<Header*>(ptr) - 1;
    used -= sizeof(Header) + header->size;
    std::free(header);
  }

  void* reallocate(void* ptr, size_t size) override {
    if (!ptr) return allocate(size);
    if (size == 0) {
      deallocate(ptr);
      return nullptr;
    }
    auto* old = static_cast<Header*>(ptr) - 1;
    void* next = allocate(size);
    if (!next) return nullptr;
    std::memcpy(next, ptr, std::min(size, old->size));
    deallocate(ptr);
    return next;
  }

  size_t bytesInUse() const { return used; }
  size_t peakBytes() const { return peak; }

 private:
  struct alignas(std::max_align_t) Header {
    size_t size;
  };
  size_t used = 0;
  size_t peak = 0;
};
}  // namespace settingsrecovery
