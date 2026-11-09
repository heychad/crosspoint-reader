#pragma once

#include <cstdint>

enum class EpubRenderMode : uint8_t { Default = 0, Balanced = 1, Light = 2 };
constexpr uint8_t EPUB_RENDER_MODE_COUNT = 3;

constexpr EpubRenderMode normalizeEpubRenderMode(uint8_t value) {
  return value < EPUB_RENDER_MODE_COUNT ? static_cast<EpubRenderMode>(value) : EpubRenderMode::Default;
}
