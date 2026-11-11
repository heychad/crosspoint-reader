"""Exercise the real reader-bar renderer with a recording display."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/personalization/ReaderBarRenderer.cpp').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
code = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include "personalization/ReaderBarRenderer.h"

constexpr int SMALL_FONT_ID = 1;
struct Rect { int x, y, width, height; };
struct Board { int cornerRadius = 0, bottomCornerRadius = 0; };
namespace BoardConfig { inline Board ACTIVE; }
struct CrossPointSettings { static constexpr int HIDE_NEVER = 0; int hideBatteryPercentage = 0, clockFormat = 0; };
CrossPointSettings SETTINGS;
struct Clock { void formatTime(char* out, size_t, bool) { std::strcpy(out, "12:00"); }
               bool localTime(std::tm&) { return false; } } halClock;
struct Power { int getBatteryPercentage() { return 75; } } powerManager;
struct Metrics { int statusBarVerticalMargin = 19, progressBarMarginTop = 1, statusBarHorizontalMargin = 5;
                 int batteryWidth = 16, batteryHeight = 8; };
struct UITheme { Metrics metrics; static UITheme& getInstance() { static UITheme theme; return theme; }
                 const Metrics& getMetrics() const { return metrics; } };
struct GfxRenderer {
  int width = 480, height = 800, top = 9, right = 7, bottom = 3, left = 7;
  mutable std::vector<Rect> fills;
  mutable std::vector<Rect> text;
  mutable std::vector<std::string> textValues;
  void getOrientedViewableTRBL(int* t, int* r, int* b, int* l) const { *t=top; *r=right; *b=bottom; *l=left; }
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int getLineHeight(int) const { return 10; }
  int getTextWidth(int, const char* s) const { return static_cast<int>(std::strlen(s)) * 5; }
  void drawRect(int, int, int, int) const {}
  void drawLine(int, int, int, int) const {}
  void drawText(int, int x, int y, const char* s) const { text.push_back({x,y,getTextWidth(0,s),10}); textValues.emplace_back(s); }
  void fillRect(int x, int y, int w, int h, bool) const { fills.push_back({x,y,w,h}); }
};
struct Gui { void drawBatteryLeft(GfxRenderer&, Rect, bool) {} } GUI;
size_t utf8SafeTruncateBuffer(const char*, size_t n) { return n; }
''' + source + r'''

int main() {
  ReaderBar top; top.progress = 0; top.thickness = 0;
  GfxRenderer screen;
  readerbars::draw(screen,top,ReaderBarPosition::Top,41,57,122,false,"","",false);
  assert(screen.fills.size()==1);
  const auto first=screen.fills[0];
  // Progress reaches the screen edge and the top bar moves up one more pixel.
  assert(first.x==0 && first.y==6 && first.width==196 && first.height==2);
  screen.fills.clear();
  readerbars::draw(screen,top,ReaderBarPosition::Top,0,0,0,false,"","",false);
  assert(screen.fills.empty());  // No visible unfilled track.
  readerbars::draw(screen,top,ReaderBarPosition::Top,100,0,0,false,"","",false);
  assert(screen.fills.size()==1 && screen.fills[0].x==0 && screen.fills[0].width==480);

  ReaderBar bottom; bottom.progress=1; bottom.thickness=1; bottom.slots[0]=ReaderBarItem::ChapterPages;
  screen.fills.clear();
  readerbars::draw(screen,bottom,ReaderBarPosition::Bottom,41,57,122,false,"","",false);
  assert(screen.fills.size()==1);
  assert(screen.fills[0].x==0 && screen.fills[0].y==793 && screen.fills[0].width==224 && screen.fills[0].height==4);

  // The top adjustment cannot move drawing above the framebuffer.
  screen.top=0; screen.fills.clear();
  readerbars::draw(screen,top,ReaderBarPosition::Top,100,0,0,false,"","",false);
  assert(screen.fills.size()==1 && screen.fills[0].y>=0);
  // Text remains in the reserved lane rather than changing page layout.
  top.slots[0]=ReaderBarItem::ChapterPages; top.slots[3]=ReaderBarItem::BookTitle; screen.top=9; screen.text.clear();
  readerbars::draw(screen,top,ReaderBarPosition::Top,41,0,0,false,"Book","",false);
  assert(screen.text.size()==2 && screen.text[0].x==8);
  assert(screen.text[0].y>=0 && screen.text[0].y+screen.text[0].height<=9+24);

  // The duration slot renders its value; an unavailable estimate uses a dash.
  ReaderBar times; times.slots[0]=ReaderBarItem::TimeLeftBook;
  screen.textValues.clear();
  readerbars::draw(screen,times,ReaderBarPosition::Top,0,0,0,false,"","",false,nullptr,"~2h 5 min");
  assert(screen.textValues.size()==1 && screen.textValues[0]=="~2h 5 min");
  screen.textValues.clear();
  readerbars::draw(screen,times,ReaderBarPosition::Top,0,0,0,false,"","",false);
  assert(screen.textValues.size()==1 && screen.textValues[0]=="-");

#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
  screen.textValues.clear();
  ReaderBar chapter;chapter.slots[ReaderBar::CENTER]=ReaderBarItem::TimeLeftChapter;
  readerbars::draw(screen,chapter,ReaderBarPosition::Top,0,0,0,false,"","",false,nullptr,nullptr,nullptr,"~12 min");
  assert(screen.textValues.size()==1 && screen.textValues[0]=="~12 min");
  screen.textValues.clear();
  readerbars::draw(screen,chapter,ReaderBarPosition::Top,0,0,0,false,"","",false);
  assert(screen.textValues.size()==1 && screen.textValues[0]=="-");
#endif
  // The viewable insets rotate with the panel, including the short upper inset.
  for (const auto insets : {std::array<int,4>{3,7,9,7}, std::array<int,4>{7,9,7,3},
                            std::array<int,4>{7,3,7,9}}) {
    GfxRenderer rotated;
    rotated.width = insets[0]==7 ? 800 : 480;
    rotated.height = insets[0]==7 ? 480 : 800;
    rotated.top=insets[0]; rotated.right=insets[1]; rotated.bottom=insets[2]; rotated.left=insets[3];
    ReaderBar line; line.progress=0; line.thickness=0;
    readerbars::draw(rotated,line,ReaderBarPosition::Top,100,0,0,false,"","",false);
    assert(rotated.fills.size()==1);
    const auto& full=rotated.fills[0];
    assert(full.x==0 && full.width==rotated.width);
    assert(full.y>=0 && full.y+full.height<=rotated.height);
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', *(['-DCROSSPOINT_EXPERIMENTAL_CHAPTER_TIME=1'] if os.getenv('TEST_CHAPTER_TIME') else []), '-I', str(root / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')],
                   check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Reader bar geometry checks passed')
