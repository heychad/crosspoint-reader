"""Check EPUB recovery policy and its production reader retry decisions."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = source.index('bool EpubReaderActivity::tryRenderRecovery()')
opening = source.index('{', start)
depth = 0
for position in range(opening, len(source)):
    depth += (source[position] == '{') - (source[position] == '}')
    if depth == 0:
        method = source[start:position + 1]
        break
else:
    raise ValueError('tryRenderRecovery')
load_failure = source.split('auto p = section->loadPage(section->currentPage);', 1)[1].split(
    '    pageLoadRetryCount = 0;\n\n', 1)[0]
code = r'''
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include "personalization/EpubRenderRecovery.h"
struct Settings { ReaderRenderSpec readerRenderSpec(int,int){return {};} } SETTINGS;
#define LOG_ERR(...) ((void)0)
struct Section { EpubBuildFailure failure; int currentPage=91;
 void abandonBuild(){currentPage=0;} void clearCache(){} EpubBuildFailure lastBuildFailure(){return failure;} };
struct EpubReaderActivity {
  std::unique_ptr<Section> section; EpubRenderRecovery renderRecovery;
  int buildViewportWidth=480,buildViewportHeight=800,currentSpineIndex=2,visibleOffsetSpine=2,cachedSpineIndex=2;
  bool pendingPercentJump=false,buildPopupPending=true,partialRebuildStartFailed=true,buildHeapPaused=true;
  bool automaticPageTurnActive=true,updated=false;int backgroundBuildFailedSpine=2;
  std::optional<unsigned> pendingPageJump,pendingOffsetJump,currentPageVisibleOffset=123,cachedVisibleTextOffset=100;
  struct Renderer {
    void clearScreen(){} void displayBuffer(){}
    void drawCenteredText(int,int,int,bool,int){}
  } renderer;
  struct EpdFontFamily {static constexpr int BOLD=1;};
  static constexpr int UI_12_FONT_ID=0, STR_PAGE_LOAD_ERROR=0, MAX_PAGE_LOAD_RETRIES=3;
  int pageLoadRetryCount=0,nextPageNumber=7;
  static int tr(int id){return id;}
  void showPendingSyncSaveError(){}
  void readFailure();
  std::string pendingAnchor;
  void requestUpdate(){updated=true;} bool tryRenderRecovery();
};
''' + method + '\nvoid EpubReaderActivity::readFailure(){ bool p=false;\n' + load_failure + '}\n' + r'''
int main(){
  for(int failed:{0,3}) {
    EpubReaderActivity retry;
    retry.pageLoadRetryCount=failed;
    retry.section=std::make_unique<Section>(Section{EpubBuildFailure::InputOutput});
    retry.readFailure();
    assert(!retry.section && retry.nextPageNumber==91 && retry.updated==(failed==0));
    assert(!retry.pendingOffsetJump);
  }
  ReaderRenderSpec base; EpubRenderRecovery policy;
  assert(!policy.retry(EpubBuildFailure::InputOutput,base,1));
  assert(policy.retry(EpubBuildFailure::OutOfMemory,base,1));
  assert(policy.spec(base,1).renderMode==EpubRenderMode::Balanced);
  assert(policy.spec(base,2).renderMode==EpubRenderMode::Default);
  assert(!policy.pageRendered(2)); assert(policy.pageRendered(1));
  assert(policy.spec(base,2).renderMode==EpubRenderMode::Balanced);
  assert(policy.retry(EpubBuildFailure::OutOfMemory,base,2));
  assert(policy.spec(base,2).renderMode==EpubRenderMode::Light);
  assert(policy.retry(EpubBuildFailure::OutOfMemory,base,2));
  assert(policy.spec(base,2).renderMode==EpubRenderMode::Light && !policy.spec(base,2).embeddedStyle);
  assert(!policy.retry(EpubBuildFailure::OutOfMemory,base,2));
  assert(policy.pageRendered(2) && policy.safeActive() && !policy.spec(base,3).embeddedStyle);
  ReaderRenderSpec alreadySafe; alreadySafe.renderMode=EpubRenderMode::Light; alreadySafe.embeddedStyle=false;
  EpubRenderRecovery noChange; assert(!noChange.retry(EpubBuildFailure::OutOfMemory,alreadySafe,1));
  policy.reset(); assert(policy.spec(base,1).renderMode==EpubRenderMode::Default);
  EpubReaderActivity reader; reader.section=std::make_unique<Section>(Section{EpubBuildFailure::OutOfMemory});
  assert(reader.tryRenderRecovery()); assert(!reader.section && reader.pendingOffsetJump==123);
  assert(reader.updated && !reader.automaticPageTurnActive && !reader.partialRebuildStartFailed);
  EpubReaderActivity other; other.visibleOffsetSpine=1; other.cachedSpineIndex=1;
  other.section=std::make_unique<Section>(Section{EpubBuildFailure::OutOfMemory});
  assert(other.tryRenderRecovery() && !other.pendingOffsetJump);
  EpubReaderActivity io; io.section=std::make_unique<Section>(Section{EpubBuildFailure::InputOutput});
  assert(!io.tryRenderRecovery() && io.section && !io.updated);
  EpubReaderActivity jump; jump.pendingPageJump=9; jump.section=std::make_unique<Section>(Section{EpubBuildFailure::OutOfMemory});
  assert(jump.tryRenderRecovery() && jump.pendingPageJump==9 && !jump.pendingOffsetJump);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), '-I', str(root / 'lib/Epub'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('EPUB recovery retries only memory failures and keeps the intended position')
