"""Run the production EPUB pixel cache and writer with checked host I/O."""

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
EPUB = ROOT / "lib/Epub/Epub"
source = (EPUB / "blocks/ImageBlock.cpp").read_text()
cache_code = source[source.index("namespace {"):source.index("}  // namespace")]

RENDERER = r"""
#pragma once
#include <array>
#include <cstdint>

struct GfxRenderer {
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  std::array<uint8_t, 48000> framebuffer{};
  RenderMode mode = BW;
  Orientation orientation = Portrait;
  int origin = 0;
  int rows = 480;
  uint8_t* getWriteTarget() { return framebuffer.data(); }
  int getWriteOriginY() const { return origin; }
  int getWriteRows() const { return rows; }
  RenderMode getRenderMode() const { return mode; }
  bool grayPlanesAreAbsolute() const { return false; }
  bool imagesDitheredInBw() const { return false; }
  int getDisplayWidth() const { return 800; }
  int getDisplayHeight() const { return 480; }
  int getDisplayWidthBytes() const { return 100; }
  Orientation getOrientation() const { return orientation; }
};
"""

DEPENDENCIES = r"""
#include "DirectPixelWriter.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
struct HalMemory {
  using PsramBuffer = std::unique_ptr<uint8_t[]>;
  static inline bool present = true;
  static PsramBuffer allocatePsram(size_t bytes) {
    return present ? std::make_unique<uint8_t[]>(bytes) : nullptr;
  }
};
struct {
  size_t internal = 8192;
  size_t getFreeHeap() const { return internal; }
  size_t getMaxAllocHeap() const { return internal; }
} ESP;

struct HalFile {
  static inline std::vector<uint8_t> contents;
  static inline size_t bytesRead = 0;
  static inline bool shortFill = false;
  static inline bool failSeek = false;
  size_t offset = 0;
  size_t size() const { return contents.size(); }
  bool seek(size_t position) {
    if (failSeek) return false;
    offset = position;
    return true;
  }
  int read(void* destination, size_t amount) {
    amount = std::min(amount, contents.size() - offset);
    if (shortFill && offset == 4 + 16384) {
      shortFill = false;
      amount -= 1;
    }
    memcpy(destination, contents.data() + offset, amount);
    offset += amount;
    bytesRead += amount;
    return static_cast<int>(amount);
  }
};
struct {
  bool openFileForRead(const char*, const std::string&, HalFile&) { return true; }
} Storage;
"""

TEST = r"""
int main() {
  constexpr size_t PAYLOAD = 96000;
  HalFile::contents.resize(PAYLOAD + 4);
  for (size_t i = 4; i < HalFile::contents.size(); ++i) HalFile::contents[i] = (i ^ (i >> 5)) & 0xff;
  GfxRenderer renderer;
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted,
                                 GfxRenderer::LandscapeClockwise, GfxRenderer::LandscapeCounterClockwise}) {
    renderer.orientation = orientation;
    const bool portrait = orientation == GfxRenderer::Portrait || orientation == GfxRenderer::PortraitInverted;
    const int width = portrait ? 480 : 800;
    const int height = portrait ? 800 : 480;
    HalFile::contents[0] = width & 0xff;
    HalFile::contents[1] = width >> 8;
    HalFile::contents[2] = height & 0xff;
    HalFile::contents[3] = height >> 8;
    for (const auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      renderer.mode = mode;
      renderer.origin = mode == GfxRenderer::BW ? 0 : 83;
      renderer.rows = mode == GfxRenderer::BW ? 480 : 80;
      renderer.framebuffer.fill(0x5a);
      HalMemory::present = true;
      HalFile::bytesRead = 0;
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(HalFile::bytesRead == PAYLOAD + 4 && pxcPsram && !pxcChunks[0]);
      const auto expected = renderer.framebuffer;
      renderer.framebuffer.fill(0x5a);
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(HalFile::bytesRead == PAYLOAD + 4 && renderer.framebuffer == expected);
      releasePxcSlot();
      assert(!pxcPsram);

      HalMemory::present = false;
      renderer.framebuffer.fill(0x5a);
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(HalFile::bytesRead == 2 * (PAYLOAD + 4) && renderer.framebuffer == expected);
      assert(!pxcChunks[0]);

      ESP.internal = 256000;
      renderer.framebuffer.fill(0x5a);
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(pxcChunks[0] && !pxcPsram && renderer.framebuffer == expected);
      releasePxcSlot();
      ESP.internal = 8192;

      HalMemory::present = true;
      HalFile::shortFill = true;
      renderer.framebuffer.fill(0x5a);
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(!pxcPsram && !pxcChunks[0] && renderer.framebuffer == expected);
      HalFile::shortFill = true;
      HalFile::failSeek = true;
      renderer.framebuffer.fill(0x5a);
      assert(!renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(!pxcPsram && !pxcChunks[0]);
      assert(std::all_of(renderer.framebuffer.begin(), renderer.framebuffer.end(), [](auto byte) {return byte == 0x5a;}));
      HalFile::failSeek = false;
    }
  }
}
"""

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "GfxRenderer.h").write_text(RENDERER)
    (path / "HalDisplay.h").touch()
    (path / "cache.cpp").write_text(DEPENDENCIES + cache_code + "}  // namespace\n" + TEST)
    binary = path / "cache-test"
    subprocess.run(
        [os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-DFREEINK_DEVICE_X4PRO", "-I", str(path),
         "-I", str(EPUB / "converters"), "-I", str(ROOT / "lib/GfxRenderer"),
         str(path / "cache.cpp"), "-o", str(binary)], check=True
    )
    subprocess.run([str(binary)], check=True)
