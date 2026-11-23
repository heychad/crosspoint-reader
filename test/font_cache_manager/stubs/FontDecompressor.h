#pragma once

#include <cstdint>
#include <cstdio>

#include "EpdFontFamily.h"

class FontDecompressor {
 public:
  static constexpr uint8_t MAX_PAGE_SLOTS = 8;
  struct PrewarmCall {
    const EpdFontData* fontData = nullptr;
    char text[32] = {};
    bool admitted = false;
  };

  void clearCache() {
    ++clearCount;
    admittedSlots = 0;
  }
  int prewarmCache(const EpdFontData* fontData, const char* text) {
    auto& call = prewarmCalls[prewarmCallCount++];
    call.fontData = fontData;
    std::snprintf(call.text, sizeof(call.text), "%s", text);
    call.admitted = admittedSlots < MAX_PAGE_SLOTS;
    if (!call.admitted) return -1;
    ++admittedSlots;
    if (order) ++*order;
    return 0;
  }
  void logStats(const char*) {}
  void resetStats() {}

  PrewarmCall prewarmCalls[MAX_PAGE_SLOTS * 2] = {};
  int* order = nullptr;
  int admittedSlots = 0;
  int prewarmCallCount = 0;
  int clearCount = 0;
};
