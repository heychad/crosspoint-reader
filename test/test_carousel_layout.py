"""Exercise Carousel cover hit targets and selection wraparound."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r'''
#include <cassert>
#include "components/themes/lyra/CarouselLayout.h"
int main() {
  using namespace carousellayout;
  auto layout = make(10, 56, 460, 480, 16);
  assert(layout.center.y + layout.center.height + 23 + 16 + 3 + 6 + 16 <= 56 + 480);
  int cx = layout.center.x + layout.center.width / 2;
  int cy = layout.center.y + layout.center.height / 2;
  int lx = layout.left.x + layout.left.width / 2;
  int ly = layout.left.y + layout.left.height / 2;
  int rx = layout.right.x + layout.right.width / 2;
  int ry = layout.right.y + layout.right.height / 2;
  assert(hit(layout, cx, cy, 3, 1) == 1);
  assert(hit(layout, lx, ly, 3, 1) == 0);
  assert(hit(layout, rx, ry, 3, 1) == 2);
  assert(hit(layout, lx, ly, 3, 0) == 2);
  assert(hit(layout, rx, ry, 3, 2) == 0);
  assert(hit(layout, lx, ly, 2, 0) == 1);
  assert(hit(layout, rx, ry, 2, 0) == -1);
  assert(hit(layout, lx, ly, 1, 0) == -1);
  assert(hit(layout, cx, cy, 0, 0) == -1);
  assert(hit(layout, cx, cy, 3, 3) == -1);
  assert(hit(layout, layout.center.x + layout.center.width, cy, 3, 1) == -1);
  assert(hit(layout, cx, layout.center.y - 1, 3, 1) == -1);
  for (int count : {5, 6}) {
    for (int x = 10; x < 470; ++x) {
      const int target = menuHit(10, 700, 460, 64, count, x, 730);
      assert(target >= 0 && target < count);
      assert(menuTile(10, 700, 460, 64, count, target).contains(x, 730));
    }
    assert(menuHit(10, 700, 460, 64, count, 9, 730) == -1);
    assert(menuHit(10, 700, 460, 64, count, 470, 730) == -1);
    assert(menuHit(10, 700, 460, 64, count, 10, 764) == -1);
  }
  assert(menuHit(10, 700, 460, 64, 0, 10, 730) == -1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    source.write_text(PROGRAM)
    subprocess.run(['c++', '-std=c++17', '-I', str(ROOT / 'src'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Carousel cover touch targets passed')
