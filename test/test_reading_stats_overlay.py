"""Exercise the production inline-overlay methods with a host renderer stub."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
opened = "void EpubReaderActivity::openOverlay(" + source.split(
    "void EpubReaderActivity::openOverlay(", 1
)[1].split("// Grayscale panels re-render", 1)[0]
closed = "void EpubReaderActivity::closeOverlayToPage(" + source.split(
    "void EpubReaderActivity::closeOverlayToPage(", 1
)[1].split("void EpubReaderActivity::renderOverlay(", 1)[0]

harness = r'''
#include <atomic>
#include <cassert>
#include <memory>
#include <functional>
#include <utility>
int locks = 0;
std::function<void()> afterUnlock;
struct RenderLock {
  RenderLock() { ++locks; }
  ~RenderLock() {
    --locks;
    if (locks == 0 && afterUnlock) {
      auto callback = std::move(afterUnlock);
      afterUnlock = {};
      callback();
    }
  }
};
enum class ReadingStatsFrame { Page, Unavailable, EndOfBook };
struct Stats {
  bool paused = false;
  int pauses = 0, resumes = 0;
  void pause() { assert(locks > 0); paused = true; ++pauses; }
  void onRender(ReadingStatsFrame frame) { assert(locks > 0); assert(frame == ReadingStatsFrame::Page); paused = false; ++resumes; }
};
struct HalDisplay { enum { FAST_REFRESH }; };
struct BoardConfig { static bool isEegoA4() { return false; } };
bool xteink = false;
bool xteinkClassPanel() { return xteink; }
struct Renderer {
  bool storeBwBuffer() { return true; }
  void restoreBwBuffer(bool) {}
  void displayBuffer(int) {}
};
struct Nav { int top = 0; void reset(int = 0) {} };
struct ReaderToolbarUi {
  Nav n;
  ReaderToolbarUi(Renderer&) {}
  void begin() {}
  Nav& nav() { return n; }
};
struct Input { void resetHomeButtonInput() {} bool hasTouch() { return true; } };
struct Popup { void dismiss() {} };
class EpubReaderActivity {
 public:
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Stats readingStats;
  Input mappedInput;
  Renderer renderer;
  Popup overlayPopup;
  Overlay overlay = Overlay::None;
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  int focusedTool = 0, panelIndex = 0, section = 1, pagesUntilFullRefresh = 0;
  bool panelCursorShown = false, panelHoldJumped = false, overlayPageStored = false;
  bool updateRequested = false;
  std::atomic<bool> pageBufferStale{false};
  int currentTocIndex() { return 0; }
  void buildMoreActions() {}
  void settleOverlayRefresh() {}
  void renderOverlay() {}
  void pushOverlayRefresh() {}
  void discardOverlayPage() { overlayPageStored = false; }
  void requestUpdate() { updateRequested = true; }
  void openOverlay(Overlay target);
  void closeOverlayToPage();
};
''' + opened + closed + r'''
int main() {
  for (const bool deferredClose : {false, true}) {
    xteink = deferredClose;
    EpubReaderActivity reader;
    // Simulate a previously notified render at the first lock-release boundary.
    afterUnlock = [&reader] {
      RenderLock lock;
      if (reader.overlay == EpubReaderActivity::Overlay::None)
        reader.readingStats.onRender(ReadingStatsFrame::Page);
    };
    reader.openOverlay(EpubReaderActivity::Overlay::Toolbar);
    assert(reader.readingStats.paused && reader.readingStats.pauses == 1);
    reader.openOverlay(EpubReaderActivity::Overlay::More);
    assert(reader.readingStats.paused);
    reader.closeOverlayToPage();
    if (deferredClose) {
      assert(reader.updateRequested && reader.readingStats.paused);
      RenderLock lock;
      reader.readingStats.onRender(ReadingStatsFrame::Page);
    }
    assert(!reader.readingStats.paused && reader.readingStats.resumes == 1);
  }
  assert(locks == 0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "overlay.cpp").write_text(harness)
    subprocess.run(["c++", "-std=c++20", str(path / "overlay.cpp"), "-o", str(path / "overlay")], check=True)
    subprocess.run([str(path / "overlay")], check=True)
print("Inline overlay tracking checks passed")
