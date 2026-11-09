"""Exercise the production bar-settings handoff and repagination state."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
start = source.index("    case EpubReaderMenuActivity::MenuAction::READER_BARS:")
end = source.index("    case EpubReaderMenuActivity::MenuAction::", start + 10)
body = source[start:end]
code = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
int locks=0;bool failAllocation=false;
struct RenderLock {RenderLock(){++locks;}~RenderLock(){--locks;}};
#define LOG_ERR(...) ((void)0)
#define STR_READER_BARS 1
#define STR_BAR_RETURN_FROM_LINK 2
#define STR_BACK 3
#define STR_OK_BUTTON 4
const char* tr(int){return "label";}
struct ActivityResult{};struct Renderer{};struct Input{};
struct ReaderBarsSettingsActivity {ReaderBarsSettingsActivity(Renderer&,Input&){}};
struct ConfirmationActivity {ConfirmationActivity(Renderer&,Input&,const char*,const char*,const char*,const char*){}};
template<class T,class...A>std::unique_ptr<T> makeUniqueNoThrow(A&&...a){
 if(failAllocation)return{};return std::make_unique<T>(std::forward<A>(a)...);
}
struct EpubReaderMenuActivity {enum class MenuAction {READER_BARS,OTHER};};
struct Reader {
 Renderer renderer;Input mappedInput;
 struct Section {int pageCount=34,currentPage=12;};
 std::unique_ptr<Section> section=std::make_unique<Section>();
 int footnoteDepth=0;bool warningShown=false;
 int cachedSpineIndex=-1,currentSpineIndex=7,cachedChapterTotalPageCount=-1,nextPageNumber=-1;
 int anchors=0,updates=0;std::function<void(const ActivityResult&)> callback;
 void rememberCurrentContentOffset(){assert(locks>0);++anchors;}
 void requestUpdate(){++updates;}
 template<class T,class F>void startActivityForResult(std::unique_ptr<T>,F&& f){
  assert(locks==0);warningShown=std::is_same_v<T,ConfirmationActivity>;callback=std::forward<F>(f);
 }
 void open(){switch(EpubReaderMenuActivity::MenuAction::READER_BARS){
''' + body + r'''
 default:break;}}
};
int main(){
 Reader reader;reader.open();assert(reader.callback && reader.section && reader.anchors==0);
 reader.callback({});assert(!reader.section && reader.anchors==1 && locks==0);
 assert(reader.cachedSpineIndex==7 && reader.cachedChapterTotalPageCount==34 && reader.nextPageNumber==12 && reader.updates==1);
 Reader noSection;noSection.section.reset();noSection.open();noSection.callback({});
 assert(noSection.anchors==0 && noSection.updates==1);
 Reader linked;linked.footnoteDepth=1;linked.open();assert(linked.warningShown && linked.callback && linked.section);
 linked.callback({});assert(linked.section && linked.anchors==0 && linked.updates==1);
 Reader failed;failAllocation=true;failed.open();assert(!failed.callback && failed.section && failed.updates==1);
 Reader failedNotice;failedNotice.footnoteDepth=1;failedNotice.open();
 assert(!failedNotice.callback && failedNotice.section && failedNotice.anchors==0 && failedNotice.updates==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Bar settings preserve the reader position for repagination, including allocation failure")
