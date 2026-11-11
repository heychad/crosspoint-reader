"""Exercise the experimental prompt and its confirmation/retry callbacks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubCompletion.cpp').read_text()
source = source[source.index('void EpubReaderActivity::initializeCompletionPrompt()'):source.rindex('#endif')]
shared = (root / 'src/activities/reader/ReaderCompletion.cpp').read_text()
shared = shared[shared.index('void ReaderActivity::handleCompletionAttempt('):shared.rindex('#endif')]
source += shared.replace('ReaderActivity::', 'EpubReaderActivity::')
reader = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
page_turn = reader[reader.index('bool EpubReaderActivity::pageTurn('):reader.index('bool EpubReaderActivity::skipPages(')]
code = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include "personalization/CompletionPrompt.h"
#include "util/ReadFolder.h"
#include "Epub/ReferencePageMap.h"
#define CROSSPOINT_EXPERIMENTAL_COMPLETION 1
#define CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT 1
#define LOG_ERR(...) ((void)0)
enum {STR_COMPLETION_PROMPT_TITLE, STR_COMPLETION_PROMPT_BODY, STR_COMPLETION_RETRY_TITLE,
      STR_COMPLETION_RETRY_BODY, STR_CANCEL, STR_RETRY};
const char* tr(int){return "text";}
unsigned long millis(){return 1;}
namespace bleturner {void acknowledgeLinkNote(){}}
struct Settings {bool moveFinishedToReadFolder=false;} SETTINGS;
bool held=false, failAllocation=false;
struct RenderLock {RenderLock(){assert(!held);held=true;} ~RenderLock(){held=false;}};
struct Renderer{}; struct Input{};
struct ConfirmationActivity {
 ConfirmationActivity(Renderer&,Input&,const char*,const char*,const char* = "",const char* = ""){}
};
template<class T,class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args){
 if(failAllocation){failAllocation=false;return {};}
 return std::make_unique<T>(std::forward<Args>(args)...);
}
struct ActivityResult {bool isCancelled=false;};
struct ReaderStats {
 enum class CompletionResult {Applied,Unchanged,NotTracking,Busy,SaveFailed,RecoveryRequired,Unavailable};
 bool known=true, done=false;int pauses=0;
 bool completionStatus(bool& out)const{if(!known)return false;out=done;return true;}
 void pause(){++pauses;}
};
struct Txt {static bool isTxtOrMd(const std::string& path){return path.ends_with(".txt") || path.ends_with(".md");}};
struct Epub {
 size_t size=100, before=50, through=100; int spine=1;
 std::string manifest,path="/book.epub";mutable int reads=0;
 const std::string& getPath()const{return path;}
 size_t getBookSize()const{return size;}
 int getSpineIndexForSize(size_t)const{return spine;}
 size_t getCumulativeSpineItemSize(int i)const{return i==0?before:through;}
 int getSpineItemsCount()const{return 2;}
 bool getItemSize(const char*,size_t* out)const{*out=manifest.size();return !manifest.empty();}
 bool readItemContentsToStream(const char*,Print& out,size_t)const{
  ++reads;
  return out.write(reinterpret_cast<const uint8_t*>(manifest.data()),manifest.size())==manifest.size();}
};
struct Section {int currentPage=0,pageCount=10;bool building=false,partial=false;
 int estimatedTotalPages()const{return 10;}bool isBuilding()const{return building;}bool isPartial()const{return partial;}};
class EpubReaderActivity {
public:
 std::string bookPath="/book.epub";
 CompletionPrompt completionPrompt;std::shared_ptr<Epub> epub=std::make_shared<Epub>();
 std::unique_ptr<Section> section=std::make_unique<Section>(); ReaderStats readingStats;
 Renderer renderer;Input mappedInput;int currentSpineIndex=1,footnoteDepth=0,pendingManualTurn=0;
 bool pendingManualTurnTouch=false,overlay=false;unsigned long lastPageTurnTime=0;
 int nextPageNumber=0;std::optional<uint16_t> pendingPageJump;
 int updates=0, edits=0, recoveries=0, dialogs=0;bool lastTarget=true;
 ReaderStats::CompletionResult nextResult=ReaderStats::CompletionResult::Applied;
 std::function<void(const ActivityResult&)> callback;
 bool inputOverPage()const{return overlay;}
 void clearDeferredReposition(){}
 void requestUpdate(){++updates;}
 void startActivityForResult(std::unique_ptr<ConfirmationActivity>,std::function<void(const ActivityResult&)> cb){++dialogs;callback=std::move(cb);}
 ReaderStats::CompletionResult setBookCompleted(bool completed){assert(!held);++edits;lastTarget=completed;return nextResult;}
 ReaderStats::CompletionResult recoverBookCompletion(){assert(!held);++recoveries;return nextResult;}
 void initializeCompletionPrompt();void observeCompletionPosition();bool showQueuedCompletionPrompt();
 void handleCompletionAttempt(bool recovery, bool completed=true);
 bool pageTurn(bool forward);
};
''' + source + page_turn + r'''
void queue(EpubReaderActivity& r){
 r.initializeCompletionPrompt();r.section->currentPage=0;r.observeCompletionPosition();
 r.section->currentPage=9;assert(!r.pageTurn(true));assert(r.completionPrompt.queued());
}
int main(){
 EpubReaderActivity a;queue(a);assert(a.showQueuedCompletionPrompt() && a.dialogs==1 && !held && a.readingStats.pauses==1);
 auto confirm=a.callback;confirm({true});assert(a.edits==0 && a.updates==2);
 assert(a.pageTurn(true) && a.currentSpineIndex==2);
 EpubReaderActivity b;queue(b);assert(b.showQueuedCompletionPrompt());
 b.nextResult=ReaderStats::CompletionResult::RecoveryRequired;
 auto accept=b.callback;accept({false});assert(b.edits==1 && b.dialogs==2);
 b.nextResult=ReaderStats::CompletionResult::Unavailable;
 auto firstRetry=b.callback;firstRetry({false});assert(b.recoveries==1 && b.dialogs==3);
 b.nextResult=ReaderStats::CompletionResult::Applied;
 auto retry=b.callback;retry({false});assert(b.recoveries==2 && b.edits==1 && b.updates==2 && !held);
 EpubReaderActivity manual;manual.nextResult=ReaderStats::CompletionResult::SaveFailed;
 manual.handleCompletionAttempt(false,false);assert(manual.edits==1 && !manual.lastTarget && manual.dialogs==1);
 manual.nextResult=ReaderStats::CompletionResult::Applied;
 auto manualRetry=manual.callback;manualRetry({false});assert(manual.edits==2 && !manual.lastTarget);
 EpubReaderActivity c;queue(c);SETTINGS.moveFinishedToReadFolder=true;
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
 assert(c.showQueuedCompletionPrompt() && c.dialogs==1);
 EpubReaderActivity f;f.initializeCompletionPrompt();f.observeCompletionPosition();f.section->currentPage=9;
 assert(!f.pageTurn(true) && f.completionPrompt.queued());
#else
 assert(!c.showQueuedCompletionPrompt() && c.dialogs==0 && !c.completionPrompt.queued());
 EpubReaderActivity f;f.initializeCompletionPrompt();f.observeCompletionPosition();f.section->currentPage=9;
 assert(f.pageTurn(true) && f.currentSpineIndex==2 && !f.completionPrompt.queued());
#endif
 EpubReaderActivity readBook;readBook.bookPath="/READ/book.epub";queue(readBook);
 assert(readBook.showQueuedCompletionPrompt() && readBook.dialogs==1);
 SETTINGS.moveFinishedToReadFolder=false;
 EpubReaderActivity d;queue(d);failAllocation=true;
 assert(!d.showQueuedCompletionPrompt() && d.dialogs==0 && !held && d.readingStats.pauses==0);
 assert(d.completionPrompt.queueOnChapterExit(1,true));
 EpubReaderActivity e;e.epub->before=120;e.initializeCompletionPrompt();
 e.completionPrompt.observe(1,0,false);e.completionPrompt.observe(1,1,false);
 assert(!e.completionPrompt.queueOnChapterExit(1,true));
 EpubReaderActivity g;g.initializeCompletionPrompt();g.observeCompletionPosition();
 g.completionPrompt.observe(1,1.0f,false);g.readingStats.known=false;g.section->currentPage=9;
 assert(g.pageTurn(true) && !g.completionPrompt.queued());
 EpubReaderActivity h;
 h.epub->manifest=R"({"format":"x-locations","version":1,"totalLocations":1000,"totalCharacters":1000,"charactersPerReferencePage":100,"spine":[{"characterStart":0,"characterCount":995},{"characterStart":995,"characterCount":5}]})";
 h.initializeCompletionPrompt();h.completionPrompt.observe(0,0.0f,false);h.completionPrompt.observe(0,1.0f,false);
 assert(h.completionPrompt.queueOnChapterExit(0,true));
 EpubReaderActivity i;i.epub->manifest="{invalid";i.initializeCompletionPrompt();
 i.completionPrompt.observe(1,0.0f,false);i.completionPrompt.observe(1,1.0f,false);
 assert(i.completionPrompt.queueOnChapterExit(1,true));
 EpubReaderActivity j;j.epub->path="/book.txt";j.epub->manifest=h.epub->manifest;
 j.initializeCompletionPrompt();j.completionPrompt.observe(1,0.0f,false);j.completionPrompt.observe(1,1.0f,false);
 assert(j.completionPrompt.queueOnChapterExit(1,true) && j.epub->reads==0);
 EpubReaderActivity k;k.epub->manifest=h.epub->manifest;failAllocation=true;k.initializeCompletionPrompt();
 k.completionPrompt.observe(1,0.0f,false);k.completionPrompt.observe(1,1.0f,false);
 assert(k.completionPrompt.queueOnChapterExit(1,true) && k.epub->reads==0);
 EpubReaderActivity l;l.epub->manifest=std::string(65537,' ');l.initializeCompletionPrompt();
 l.completionPrompt.observe(1,0.0f,false);l.completionPrompt.observe(1,1.0f,false);
 assert(l.completionPrompt.queueOnChapterExit(1,true) && l.epub->reads==0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    for flags in ([], ['-DCROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION']):
      subprocess.run(['c++', '-std=c++20', *flags, '-I', str(root / 'src'),
                    '-I', str(root / 'test/reference_pages/stubs'),
                    '-I', str(root / 'test/chapter_html_slim_parser/stubs'),
                    '-I', str(root / 'lib/Epub'), '-I', str(root / 'freeink-sdk/libs/network/JsonSax/include'),
                    str(path / 'test.cpp'), str(root / 'lib/Epub/Epub/ReferencePageMap.cpp'),
                    str(root / 'freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp'),
                    '-o', str(path / 'test')], check=True)
      subprocess.run([str(path / 'test')], check=True, timeout=10)
print('EPUB completion prompt checks passed')
