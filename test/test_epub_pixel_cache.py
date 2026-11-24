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
  bool ditherImages = false;
  bool imagesDitheredInBw() const { return ditherImages; }
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
#include <utility>
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
void makePackedImage(int width, int height, size_t seed = 0) {
  HalFile::contents.resize(4 + static_cast<size_t>((width + 3) / 4) * height);
  HalFile::contents[0] = width & 0xff;
  HalFile::contents[1] = width >> 8;
  HalFile::contents[2] = height & 0xff;
  HalFile::contents[3] = height >> 8;
  for (size_t i = 4; i < HalFile::contents.size(); ++i) HalFile::contents[i] = ((i ^ (i >> 5)) + seed) & 0xff;
}

// Paint every source pixel and let the writer's ordinary clipping choose
// the visible ones. This reference does not use the optimized row/column ranges.
void paintReference(GfxRenderer& renderer, int x, int y, int width, int height) {
  DirectPixelWriter writer;
  writer.init(renderer);
  for (int row = 0; row < height; ++row) {
    writer.beginRow(y + row);
    const auto* pixels = HalFile::contents.data() + 4 + row * ((width + 3) / 4);
    for (int col = 0; col < width; ++col) {
      const uint8_t pixel = (pixels[col / 4] >> (6 - (col % 4) * 2)) & 3;
      writer.writePixel(x + col, pixel);
    }
  }
}

int main() {
  constexpr size_t PAYLOAD = 96000;
  GfxRenderer renderer;
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted,
                                 GfxRenderer::LandscapeClockwise, GfxRenderer::LandscapeCounterClockwise}) {
    renderer.orientation = orientation;
    const bool portrait = orientation == GfxRenderer::Portrait || orientation == GfxRenderer::PortraitInverted;
    const int width = portrait ? 480 : 800;
    const int height = portrait ? 800 : 480;
    makePackedImage(width, height);
    for (const auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      renderer.mode = mode;
      renderer.origin = mode == GfxRenderer::BW ? 0 : 83;
      renderer.rows = mode == GfxRenderer::BW ? 480 : 80;
      const size_t streamed = portrait ? PAYLOAD : static_cast<size_t>(renderer.rows) * (width / 4);
      renderer.framebuffer.fill(0x5a);
      paintReference(renderer, 0, 0, width, height);
      const auto expected = renderer.framebuffer;
      renderer.framebuffer.fill(0x5a);
      HalMemory::present = true;
      HalFile::bytesRead = 0;
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(HalFile::bytesRead == PAYLOAD + 4 && pxcPsramCount == 1 && !pxcChunks[0]);
      assert(renderer.framebuffer == expected);
      renderer.framebuffer.fill(0x5a);
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(HalFile::bytesRead == PAYLOAD + 4 && renderer.framebuffer == expected);
      releasePxcSlot();
      assert(pxcPsramCount == 0 && pxcPsramBytes == 0 && !pxcPsramSlots[0].pixels);

      HalMemory::present = false;
      renderer.framebuffer.fill(0x5a);
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(HalFile::bytesRead == PAYLOAD + 4 + streamed + 4 && renderer.framebuffer == expected);
      assert(!pxcChunks[0]);

      ESP.internal = 256000;
      renderer.framebuffer.fill(0x5a);
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(pxcChunks[0] && pxcPsramCount == 0 && renderer.framebuffer == expected);
      releasePxcSlot();
      ESP.internal = 8192;

      HalMemory::present = true;
      HalFile::shortFill = true;
      renderer.framebuffer.fill(0x5a);
      assert(renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(pxcPsramCount == 0 && !pxcChunks[0] && renderer.framebuffer == expected);
      HalFile::shortFill = true;
      HalFile::failSeek = true;
      renderer.framebuffer.fill(0x5a);
      assert(!renderFromCache(renderer, "/book/image.pxc", 0, 0, width, height));
      assert(pxcPsramCount == 0 && !pxcChunks[0]);
      assert(std::all_of(renderer.framebuffer.begin(), renderer.framebuffer.end(), [](auto byte) {return byte == 0x5a;}));
      HalFile::failSeek = false;
    }
  }

  // A BW image contributes nothing to either text-AA plane. A cached
  // payload skips the row walk; an unreadable on-card cache still fails.
  for (auto mode : {GfxRenderer::GRAYSCALE_MSB, GfxRenderer::GRAYSCALE_LSB}) {
    renderer.ditherImages = true;
    renderer.mode = mode;
    renderer.orientation = GfxRenderer::LandscapeCounterClockwise;
    renderer.origin = 0;
    renderer.rows = 480;
    renderer.framebuffer.fill(0xa5);
    const auto original = renderer.framebuffer;
    makePackedImage(160, 80);
    unsigned visits = 0;
    renderPxcRows(renderer, 0, 0, {3, 160, 80}, [&](size_t position, int) {
      ++visits;
      return HalFile::contents.data() + 4 + position;
    });
    assert(visits == 0 && renderer.framebuffer == original);
    HalMemory::present = false;
    ESP.internal = 0;
    HalFile::bytesRead = 0;
    assert(renderFromCache(renderer, "/book/dither.pxc", 0, 0, 160, 80));
    assert(HalFile::bytesRead == 4 + 40 * 80 && renderer.framebuffer == original);
    HalFile::failSeek = true;
    assert(!renderFromCache(renderer, "/book/dither.pxc", 0, 0, 160, 80));
    HalFile::failSeek = false;
  }
  renderer.ditherImages = false;
  ESP.internal = 8192;

  // A smaller image touches only the edge of these landscape strips.
  HalMemory::present = false;
  makePackedImage(320, 53);
  renderer.rows = 40;
  renderer.mode = GfxRenderer::GRAYSCALE_MSB;
  for (const auto orientation : {GfxRenderer::LandscapeClockwise, GfxRenderer::LandscapeCounterClockwise}) {
    renderer.orientation = orientation;
    const bool clockwise = orientation == GfxRenderer::LandscapeClockwise;
    renderer.origin = clockwise ? 369 : 139;
    renderer.framebuffer.fill(0x5a);
    paintReference(renderer, 17, 99, 320, 53);
    const auto expected = renderer.framebuffer;
    renderer.framebuffer.fill(0x5a);
    HalFile::bytesRead = 0;
    assert(renderFromCache(renderer, "/book/small.pxc", 17, 99, 320, 53));
    assert(HalFile::bytesRead == 4 + (clockwise ? 12 : 13) * 80);
    assert(renderer.framebuffer == expected);

    renderer.origin = 0;
    HalFile::bytesRead = 0;
    HalFile::failSeek = true;
    assert(renderFromCache(renderer, "/book/small.pxc", 17, 99, 320, 53));
    assert(HalFile::bytesRead == 4 && renderer.framebuffer == expected);
    HalFile::failSeek = false;
  }

  makePackedImage(0, 1);
  assert(!renderFromCache(renderer, "/book/invalid.pxc", 17, 99, 1, 1));

  // Each image has distinct pixels. All three passes should agree with the
  // reference while reading each cache file just once.
  HalMemory::present = true;
  HalFile::bytesRead = 0;
  renderer.orientation = GfxRenderer::Portrait;
  renderer.origin = 0;
  renderer.rows = 480;
  GfxRenderer reference = renderer;
  constexpr int WIDTH = 320;
  constexpr int HEIGHT = 160;
  constexpr size_t IMAGE_BYTES = (WIDTH / 4) * HEIGHT;
  const char* paths[] = {"/book/first.pxc", "/book/second.pxc", "/book/third.pxc"};
  for (const auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.mode = reference.mode = mode;
    renderer.framebuffer.fill(0x5a);
    reference.framebuffer.fill(0x5a);
    for (int i = 0; i < 3; i++) {
      makePackedImage(WIDTH, HEIGHT, i);
      const int y = i * (HEIGHT + 10);
      paintReference(reference, 20, y, WIDTH, HEIGHT);
      assert(renderFromCache(renderer, paths[i], 20, y, WIDTH, HEIGHT));
    }
    assert(renderer.framebuffer == reference.framebuffer);
    assert(pxcPsramCount == 3 && pxcPsramBytes == 3 * IMAGE_BYTES);
    assert(HalFile::bytesRead == 3 * (IMAGE_BYTES + 4));
  }

  // A short fill falls back to streaming without dropping earlier images.
  HalFile::shortFill = true;
  makePackedImage(400, 200, 3);
  assert(renderFromCache(renderer, "/book/fourth.pxc", 20, 530, 400, 200));
  assert(!HalFile::shortFill && pxcPsramCount == 3 && !pxcPsramSlots[3].pixels);
  assert(pxcPsramBytes == 3 * IMAGE_BYTES);
  assert(renderFromCache(renderer, "/book/fourth.pxc", 20, 530, 400, 200));
  assert(pxcPsramCount == 4 && pxcPsramBytes == 3 * IMAGE_BYTES + 20000);

  // Images beyond the slot count still render and do not evict others.
  makePackedImage(WIDTH, HEIGHT, 4);
  const auto beforeOverflow = HalFile::bytesRead;
  assert(renderFromCache(renderer, "/book/fifth.pxc", 20, 20, WIDTH, HEIGHT));
  assert(HalFile::bytesRead == beforeOverflow + IMAGE_BYTES + 4);
  assert(renderFromCache(renderer, paths[2], 20, 340, WIDTH, HEIGHT));
  assert(HalFile::bytesRead == beforeOverflow + IMAGE_BYTES + 4);
  assert(pxcPsramCount == 4 && pxcPsramBytes == 3 * IMAGE_BYTES + 20000);
  releasePxcSlot();
  assert(pxcPsramCount == 0 && pxcPsramBytes == 0);
  for (const auto& slot : pxcPsramSlots) assert(!slot.pixels);

  // The byte cap also applies before all slots are filled.
  makePackedImage(432, 600);
  assert(renderFromCache(renderer, paths[0], 0, 0, 432, 600));
  assert(pxcPsramCount == 1 && pxcPsramBytes == 64800);
  makePackedImage(360, 400);
  const auto beforeBudgetCheck = HalFile::bytesRead;
  ESP.internal = 256000;
  for (int i = 0; i < 2; i++) assert(renderFromCache(renderer, paths[1], 0, 0, 360, 400));
  assert(HalFile::bytesRead == beforeBudgetCheck + 2 * (36000 + 4));
  assert(pxcPsramCount == 1 && pxcPsramBytes == 64800 && !pxcChunks[0]);

  // A cached path is only used when its dimensions match the requested block.
  makePackedImage(432, 600);
  assert(!renderFromCache(renderer, paths[0], 0, 0, 410, 600));
  releasePxcSlot();
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
