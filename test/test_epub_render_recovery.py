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
code = r'''
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include "personalization/EpubRenderRecovery.h"
struct Settings { ReaderRenderSpec readerRenderSpec(int,int){return {};} } SETTINGS;
struct Section { EpubBuildFailure failure; EpubBuildFailure lastBuildFailure(){return failure;} };
struct EpubReaderActivity {
  std::unique_ptr<Section> section; EpubRenderRecovery renderRecovery;
  int buildViewportWidth=480,buildViewportHeight=800,currentSpineIndex=2,visibleOffsetSpine=2,cachedSpineIndex=2;
  bool pendingPercentJump=false,buildPopupPending=true,partialRebuildStartFailed=true,buildHeapPaused=true;
  bool automaticPageTurnActive=true,updated=false;int backgroundBuildFailedSpine=2;
  std::optional<unsigned> pendingPageJump,pendingOffsetJump,currentPageVisibleOffset=123,cachedVisibleTextOffset=100;
  std::string pendingAnchor;
  void requestUpdate(){updated=true;} bool tryRenderRecovery();
};
''' + method + r'''
int main(){
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
