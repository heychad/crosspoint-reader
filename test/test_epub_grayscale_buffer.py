"""Exercise the EPUB grayscale buffer and fallback with a simulated display."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
source = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
start = source.index("  if (tiledGrayscale) {", source.index("void EpubReaderActivity::renderContents("))
end = source.index("  } else {\n    if (needsAnyGrayscale)", start)
tiled_body = source[start:end] + "  }\n"

fixtures = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>

#define LOG_DBG(origin, format, ...) ((void)0)
#define LOG_ERR(origin, format, ...) ((void)0)

constexpr int WIDTH_BYTES = 100;
constexpr int HEIGHT = 480;
constexpr size_t PLANE_BYTES = WIDTH_BYTES * HEIGHT;

struct HalDisplay {
  enum class Controller { SSD1677, Other };
  Controller controller = Controller::SSD1677;
  Controller getController() const { return controller; }
} display;

struct HalMemory {
  static inline int slots = 0;
  static inline int active = 0;
  struct Deleter {
    void operator()(uint8_t* pointer) const {
      --active;
      delete[] pointer;
    }
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], Deleter>;
  static PsramBuffer allocatePsram(size_t bytes) {
    assert(bytes == PLANE_BYTES);
    if (!slots) return {};
    --slots;
    ++active;
    return PsramBuffer(new uint8_t[bytes]);
  }
};
struct {
  size_t heap = 0;
  size_t getFreeHeap() const { return heap; }
  size_t getMaxAllocHeap() const { return heap; }
} ESP;
bool allowHeap = true;
template <typename T> std::unique_ptr<T> makeUniqueNoThrow(size_t bytes) {
  return allowHeap ? std::make_unique<T>(bytes) : nullptr;
}

unsigned long millis() {
  static unsigned long now = 0;
  return ++now;
}
uint8_t reference(bool lsb, int y, int x) {
  return static_cast<uint8_t>((lsb ? 11 : 217) + y * 29 + x * 17);
}
struct GfxRenderer {
  enum Mode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  Mode mode = BW;
  uint8_t* target = nullptr;
  int first = 0, rows = 0;
  std::array<int, 2> uploadedRows{};
  std::array<uint8_t, PLANE_BYTES> blackAndWhite{};
  int paints = 0, paintsBeforeWait = -1, uploads = 0, grayDisplays = 0, cleanups = 0;
  int getDisplayWidthBytes() const { return WIDTH_BYTES; }
  int getDisplayHeight() const { return HEIGHT; }
  void setRenderMode(Mode next) {
    assert(!target);
    mode = next;
  }
  void beginStripTarget(uint8_t* buffer, int y, int count) {
    assert(buffer && !target && y >= 0 && count > 0 && y + count <= HEIGHT);
    assert(mode != BW);
    target = buffer;
    first = y;
    rows = count;
  }
  void clearScreen(uint8_t color) {
    assert(target && color == 0);
    std::fill_n(target, rows * WIDTH_BYTES, color);
  }
  void paint() {
    ++paints;
    for (int row = 0; row < rows; ++row)
      for (int x = 0; x < WIDTH_BYTES; ++x)
        target[row * WIDTH_BYTES + x] |= reference(mode == GRAYSCALE_LSB, first + row, x);
  }
  void endStripTarget() { target = nullptr; }
  void waitRefreshComplete() {
    assert(paintsBeforeWait < 0);
    paintsBeforeWait = paints;
  }
  void writeGrayscalePlaneStrip(bool lsb, const uint8_t* buffer, int y, int count) {
    assert(paintsBeforeWait >= 0 && !target);
    assert(lsb || uploadedRows[0] == HEIGHT);
    int& next = uploadedRows[lsb ? 0 : 1];
    assert(y == next && count > 0 && y + count <= HEIGHT);
    for (int row = 0; row < count; ++row)
      for (int x = 0; x < WIDTH_BYTES; ++x)
        assert(buffer[row * WIDTH_BYTES + x] == reference(lsb, y + row, x));
    next += count;
    ++uploads;
  }
  void displayGrayBuffer() {
    assert(mode == BW && uploadedRows[0] == HEIGHT && uploadedRows[1] == HEIGHT);
    ++grayDisplays;
  }
  void cleanupGrayscaleWithFrameBuffer() {
    assert(mode == BW && !target);
    assert(std::all_of(blackAndWhite.begin(), blackAndWhite.end(), [](auto byte) { return byte == 0x9a; }));
    ++cleanups;
  }
};

void render(GfxRenderer& renderer, bool overlapRefresh, bool combinedGrayscaleBase = false) {
  const bool tiledGrayscale = true;
  const auto t0 = millis(), tPrewarm = millis(), tBwRender = millis(), tDisplay = millis();
  const auto renderGrayscalePass = [&] { renderer.paint(); };
""" + tiled_body + r"""
}

void check(int slots, size_t heap, bool overlap, bool scratch, int paints, int beforeWait, int uploads) {
  GfxRenderer renderer;
  renderer.blackAndWhite.fill(0x9a);
  HalMemory::slots = slots;
  ESP.heap = heap;
  allowHeap = scratch;
  render(renderer, overlap);
  assert(renderer.paints == paints && renderer.paintsBeforeWait == beforeWait);
  assert(renderer.uploads == uploads && renderer.grayDisplays == (uploads > 0));
  assert(renderer.cleanups == 1 && !renderer.target && HalMemory::active == 0);
}

int main() {
  check(2, 0, true, true, 2, 2, 2);       // Both planes before the wait.
  check(1, 0, true, true, 2, 1, 2);       // Reuse one PSRAM plane.
  check(2, 0, false, true, 2, 1, 2);      // No overlap needs only one plane.
  assert(HalMemory::slots == 1);
  check(0, 200000, true, true, 2, 2, 2);  // Full planes from the existing heap path.
  check(0, 0, true, true, 12, 0, 12);     // Six strips per plane.
  check(0, 0, true, false, 0, 0, 0);      // Low-memory resync with no grayscale.
  display.controller = HalDisplay::Controller::Other;
  check(2, 0, true, true, 12, 0, 12);
  assert(HalMemory::slots == 2);
}
"""

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(fixtures)
    binary = path / "grayscale-buffer-test"
    subprocess.run(
        [os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-DFREEINK_DEVICE_X4PRO=1",
         *shlex.split(os.environ.get("CXXFLAGS", "")), str(path / "test.cpp"), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
