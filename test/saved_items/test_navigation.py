"""Execute production Saved Items menu mappings and legacy position resolution."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
reader = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
home = (ROOT / 'src/activities/home/HomeActivity.h').read_text()

def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

legacy = block(reader, '  if (sync.hasChapterProgress)')
resolve = block(reader, '    if (pendingPercentJump && section->pageCount > 0)')
code = r'''
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "activities/ActivityResult.h"
struct ReferencePageMap { struct Target {}; };
enum class HomeMenuItem { NONE, FILE_BROWSER, LIBRARY, SAVED_ITEMS, PLUGINS, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU };
struct Metrics { int homeTopPadding,homeCoverTileHeight,homeMenuTopOffset,buttonHintsHeight,menuSpacing;bool homeContinueReadingInMenu; };
struct Renderer {int height=800; int getScreenHeight()const{return height;}};
struct Theme {int rowHeight=64,inset=0;int getMenuRowHeight(const Renderer&)const{return rowHeight;}int getMenuTopInset()const{return inset;}} GUI;
struct UITheme {Metrics metrics{56,300,16,40,8,false};static UITheme& getInstance(){static UITheme t;return t;}const Metrics& getMetrics()const{return metrics;}};
struct HomeActivity {
 struct MenuPage {int top,first,count,capacity;};
 Renderer renderer;int selectorIndex=0;std::vector<int> recentBooks;bool extra=false;
 int getMenuItemCount()const{return 5+extra+recentBooks.size();}
'''
code += block(home, '  static int menuItemToIndex(')
code += block(home, '  static HomeMenuItem indexToMenuItem(')
code += '\n' + block((ROOT/'src/activities/home/HomeActivity.cpp').read_text(), 'HomeActivity::MenuPage HomeActivity::menuPage(')
code = code.replace('HomeActivity::MenuPage HomeActivity::menuPage()', 'MenuPage menuPage()')
code += r'''
};
struct RenderLock {};
struct Epub { int getSpineItemsCount() const {return 4;} };
struct Section {
  int pageCount=20,currentPage=0;
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t index) {return index == 4 ? std::optional<uint16_t>(7) : std::nullopt;}
  std::optional<uint16_t> getReferenceTargetPage(){return std::nullopt;}
};
struct EpubReaderActivity {
  std::unique_ptr<Epub> epub=std::make_unique<Epub>();
  std::unique_ptr<Section> section=std::make_unique<Section>();
  std::optional<ReferencePageMap::Target> pendingReferenceJump;
  std::optional<uint32_t> pendingOffsetJump=6,cachedVisibleTextOffset=9;
  std::optional<uint16_t> pendingParagraphJump,pendingPageJump=3;
  bool pendingPercentJump=false;
  int currentSpineIndex=0,nextPageNumber=3,cachedChapterTotalPageCount=20,pendingManualTurn=1;
  float pendingSpineProgress=0;
  std::string pendingAnchor="old";
  void requestUpdate() {}
  void clearDeferredReposition();
  void applyProgressChange(const ProgressChangeResult& sync) {
'''
code += legacy
code += '\n}\n void resolve() {' + resolve + '\n}\n};\n'
code += block(reader, 'void EpubReaderActivity::clearDeferredReposition(')
code += r'''
int main() {
  for (auto slot : {HomeMenuItem::NONE, HomeMenuItem::PLUGINS, HomeMenuItem::OPDS_BROWSER}) for (bool libraryFirst : {false, true}) {
    const HomeMenuItem expected[] = {libraryFirst ? HomeMenuItem::LIBRARY : HomeMenuItem::FILE_BROWSER,
                                    libraryFirst ? HomeMenuItem::FILE_BROWSER : HomeMenuItem::LIBRARY, HomeMenuItem::SAVED_ITEMS,
                                    slot,HomeMenuItem::FILE_TRANSFER,HomeMenuItem::SETTINGS_MENU};
    int index=0;
    for (auto item : expected) {
      if (item == HomeMenuItem::NONE) continue;
      assert(HomeActivity::menuItemToIndex(item,slot,libraryFirst)==index);
      assert(HomeActivity::indexToMenuItem(index++,slot,libraryFirst)==item);
    }
    assert(HomeActivity::indexToMenuItem(index,slot,libraryFirst)==HomeMenuItem::NONE);
  }
  HomeActivity home;
  for (int height : {320,480,792,800}) {
    home.renderer.height=height;
    for (bool extra : {false,true}) {
      home.extra=extra;
      for (bool continuing : {false,true}) {
        auto& m=UITheme::getInstance().metrics;
        m.homeContinueReadingInMenu=continuing;
        for (int theme=0;theme<3;++theme) {
          GUI.rowHeight=theme==0?45:64;GUI.inset=theme==0?8:0;
          m.homeCoverTileHeight=theme==0?400:300;
          for (int recent : {0,1,3}) {
            home.recentBooks.resize(recent);
            for (int selected=0;selected<home.getMenuItemCount();++selected) {
              home.selectorIndex=selected;
              const auto page=home.menuPage();
              assert(page.count>0 && page.first>=0);
              const int lastBottom=page.top+(page.count-1)*(GUI.rowHeight+m.menuSpacing)+GUI.rowHeight;
              assert(lastBottom<=height-m.buttonHintsHeight);
              const int base=continuing?0:recent;
              assert(page.first+page.count<=home.getMenuItemCount()-base);
              if(selected>=base) assert(selected-base>=page.first && selected-base<page.first+page.count);
            }
          }
        }
      }
    }
  }
  EpubReaderActivity reader;
  ProgressChangeResult target;
  target.hasChapterProgress=true;target.spineIndex=2;target.chapterProgress=.8f;target.paragraphIndex=4;
  reader.applyProgressChange(target);
  assert(reader.currentSpineIndex==2 && !reader.section && reader.pendingPercentJump);
  assert(!reader.pendingPageJump && reader.pendingAnchor.empty() && reader.pendingManualTurn==0);
  assert(!reader.cachedVisibleTextOffset && !reader.pendingOffsetJump);
  reader.section=std::make_unique<Section>();reader.resolve();
  assert(reader.section->currentPage==7 && reader.nextPageNumber==7 && !reader.pendingPercentJump && !reader.pendingParagraphJump);
  target.paragraphIndex=99;
  reader.applyProgressChange(target);reader.section=std::make_unique<Section>();reader.resolve();
  assert(reader.section->currentPage==16);
  target.paragraphIndex=UINT16_MAX;target.chapterProgress=1;
  reader.applyProgressChange(target);reader.section=std::make_unique<Section>();reader.resolve();
  assert(reader.section->currentPage==19);
  target.spineIndex=4;reader.applyProgressChange(target);assert(reader.section);
  target.spineIndex=1;target.chapterProgress=std::nanf("");reader.applyProgressChange(target);assert(reader.section);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory/'navigation.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS','')),
                    '-I', str(ROOT/'src'), '-I', str(ROOT/'lib/Epub'), str(directory/'navigation.cpp'),
                    '-o', str(directory/'navigation')], check=True)
    subprocess.run([str(directory/'navigation')], check=True)
print('Saved Items menu routes, paragraph targets, fallback fractions and stale-position clearing passed')
