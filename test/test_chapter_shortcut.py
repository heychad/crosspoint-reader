"""Exercise the production EPUB chapter picker without a device."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = source.index('void EpubReaderActivity::openChapterSelection(')
end = source.index('\nvoid EpubReaderActivity::', start + 1)
method = source[start:end]
program = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#define LOG_ERR(...) ((void)0)
struct RenderLock {};
struct ChapterResult { int spineIndex; std::string anchor; };
struct ActivityResult { bool isCancelled=false; std::variant<ChapterResult> data; };
struct Section { int pageCount=100,currentPage=39; };
struct EpubReaderChapterSelectionActivity {
  EpubReaderChapterSelectionActivity(int,int,int,int toc){assert(toc==4);}
};
bool failAllocation=false;
template<class T,class... Args> auto makeUniqueNoThrow(Args&&... args) {
  return failAllocation ? std::unique_ptr<T>{} : std::make_unique<T>(std::forward<Args>(args)...);
}
struct EpubReaderActivity {
  int renderer=0,mappedInput=0,epub=0;
  int currentSpineIndex=2,cachedSpineIndex=-1,cachedChapterTotalPageCount=0,nextPageNumber=0;
  int remembers=0,clears=0,menus=0,updates=0;
  std::string pendingAnchor;
  std::unique_ptr<Section> section=std::make_unique<Section>();
  std::function<void(const ActivityResult&)> callback;
  int currentTocIndex(){return 4;}
  void rememberCurrentContentOffset(){++remembers;}
  void clearDeferredReposition(){++clears;}
  void openReaderMenu(){++menus;}
  void requestUpdate(){++updates;}
  void startActivityForResult(std::unique_ptr<EpubReaderChapterSelectionActivity> picker,
                             std::function<void(const ActivityResult&)> next) {
    assert(picker && !section); callback=std::move(next);
  }
  void openChapterSelection(bool);
};
''' + method + r'''
int main(){
  for(bool fromMenu : {false,true}) {
    EpubReaderActivity reader;reader.openChapterSelection(fromMenu);
    assert(!reader.section && reader.callback && reader.remembers==1);
    assert(reader.cachedSpineIndex==2 && reader.cachedChapterTotalPageCount==100 && reader.nextPageNumber==39);
    reader.callback({true,ChapterResult{0,""}});
    assert(reader.currentSpineIndex==2 && reader.nextPageNumber==39 && reader.clears==0);
    assert(reader.menus==(fromMenu?1:0) && reader.updates==(fromMenu?0:1));
    EpubReaderActivity selected;selected.openChapterSelection(fromMenu);
    selected.callback({false,ChapterResult{7,"target"}});
    assert(selected.currentSpineIndex==7 && selected.pendingAnchor=="target" && selected.nextPageNumber==0);
    assert(selected.clears==1 && selected.updates==1);
    failAllocation=true;
    EpubReaderActivity failed;auto* previous=failed.section.get();failed.openChapterSelection(fromMenu);
    assert(failed.section.get()==previous && !failed.callback && failed.currentSpineIndex==2);
    failAllocation=false;
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.cpp').write_text(program)
    subprocess.run(['c++','-std=c++20',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Chapter picker: direct/menu cancellation, selected anchor, and allocation failure preserve state')

xtc_source = (ROOT / 'src/activities/reader/XtcReaderActivity.cpp').read_text()
xtc_start = xtc_source.index('void XtcReaderActivity::openChapterSelection()')
xtc_end = xtc_source.index('bool XtcReaderActivity::handleFormatInput()', xtc_start)
xtc_program = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <variant>
#include <vector>
#include "personalization/ReaderShortcuts.h"
#define LOG_ERR(...) ((void)0)
enum class QuickLockTrigger { None };
struct PageResult { unsigned page; };
struct ActivityResult { bool isCancelled=false; std::variant<PageResult> data; };
struct Xtc {
  bool chapters=true;
  std::vector<int> items{1};
  bool hasChapters()const{return chapters;}
  const auto& getChapters()const{return items;}
};
struct XtcReaderChapterSelectionActivity {
  XtcReaderChapterSelectionActivity(int,int,std::shared_ptr<Xtc>,unsigned){}
};
bool failAllocation=false;
template<class T,class... Args> auto makeUniqueNoThrow(Args&&... args) {
  return failAllocation ? std::unique_ptr<T>{} : std::make_unique<T>(std::forward<Args>(args)...);
}
struct ReaderActivity {
  bool supportsReaderShortcut(readershortcuts::Action action)const{return action==readershortcuts::Action::Home;}
  bool handleReaderShortcut(readershortcuts::Action action,QuickLockTrigger){return action==readershortcuts::Action::Home;}
};
struct XtcReaderActivity : ReaderActivity {
  std::shared_ptr<Xtc> xtc=std::make_shared<Xtc>();
  int renderer=0,mappedInput=0,updates=0;
  unsigned currentPage=39;
  std::function<void(const ActivityResult&)> callback;
  void requestUpdate(){++updates;}
  void startActivityForResult(std::unique_ptr<XtcReaderChapterSelectionActivity> picker,
                             std::function<void(const ActivityResult&)> next){assert(picker);callback=std::move(next);}
  void openChapterSelection();
  bool supportsReaderShortcut(readershortcuts::Action)const;
  bool handleReaderShortcut(readershortcuts::Action,QuickLockTrigger);
};
''' + xtc_source[xtc_start:xtc_end] + r'''
int main(){
 using A=readershortcuts::Action;
 XtcReaderActivity r;
 assert(r.supportsReaderShortcut(A::SelectChapter));
 assert(r.handleReaderShortcut(A::SelectChapter,QuickLockTrigger::None) && r.callback);
 r.callback({true,PageResult{90}});assert(r.currentPage==39 && r.updates==0);
 r.callback({false,PageResult{90}});assert(r.currentPage==90 && r.updates==1);
 r.callback={};r.xtc->items.clear();
 assert(!r.supportsReaderShortcut(A::SelectChapter));
 r.handleReaderShortcut(A::SelectChapter,QuickLockTrigger::None);assert(!r.callback);
 r.xtc.reset();assert(!r.supportsReaderShortcut(A::SelectChapter));
 assert(r.supportsReaderShortcut(A::Home) && r.handleReaderShortcut(A::Home,QuickLockTrigger::None));
 r.xtc=std::make_shared<Xtc>();failAllocation=true;
 r.openChapterSelection();assert(!r.callback && r.currentPage==90);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.cpp').write_text(xtc_program)
    subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('XTC chapter shortcut: missing chapters, cancellation, selection, and allocation failure passed')
