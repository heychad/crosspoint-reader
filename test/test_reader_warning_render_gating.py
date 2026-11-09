"""Exercise the actual reader render and warning polling around failed pages."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/ReaderActivity.cpp').read_text()

def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for pos in range(opening, len(source)):
        depth += (source[pos] == '{') - (source[pos] == '}')
        if not depth:
            return source[start:pos + 1]
    raise ValueError(signature)

fixture = r'''
#include <StatsSaveNotice.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#define LOG_ERR(...) ((void)0)
uint32_t retained=0;
StatsSaveNotice& settingsSaveNotice(){static StatsSaveNotice n(retained);return n;}
unsigned long millis(){return 0;}
namespace trustedtime { int trustedNow(){return 0;} }
struct RenderLock {};
enum class ReadingStatsFrame {EndOfBook,Page,Unavailable};
struct Renderer {void clearScreen(){} void displayBuffer(){}};
struct EndOfBookOptions {explicit EndOfBookOptions(Renderer&){} void loadOnce(const std::string&){} void render(Renderer&,int){}};
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... args){return std::make_unique<T>(args...);}
struct Session {void onRenderComplete(unsigned long,int,int){}};
struct Stats {void onRender(ReadingStatsFrame){}};
struct ReaderActivity {
 std::atomic<bool> settingsNoticeRenderReady{false},homeButtonFeedbackVisible{false},pageRendered{false},endOfBookOptionsReady{false};
 std::atomic<uint32_t> shownSettingsNotice{0};
 uint32_t renderedPageGeneration=0;
 std::unique_ptr<EndOfBookOptions> endOfBookOptions;
 Renderer renderer;int mappedInput=0,updates=0,notices=0;bool drawSuccess=false,end=false,popup=false;
 std::string bookPath="book.epub";Session readerSession;Stats readingStats;
 void requestUpdate(){++updates;}bool isAtEndOfBook()const{return end;}
 void onEndOfBookRendered(){}void markPageRendered(){++renderedPageGeneration;pageRendered=true;}
 int getProgressBasisPoints(){return 0;}bool inputOverPage(){return false;}
 bool formatPopupVisible(){return popup;}
 void renderBook(){if(drawSuccess)markPageRendered();}
 void showNextReaderNotice(bool covered){if(!covered&&settingsSaveNotice().pending()){++notices;shownSettingsNotice=settingsSaveNotice().pending();}}
 void pollSettingsSaveWarning();void render(RenderLock&&);
};
'''
body = r'''
int main(){
 ReaderActivity r;settingsSaveNotice().recordFailure();
 r.render(RenderLock{});for(int i=0;i<100;++i)r.pollSettingsSaveWarning();
 assert(r.updates==0&&r.notices==0&&!r.settingsNoticeRenderReady);
 r.drawSuccess=true;r.render(RenderLock{});assert(r.notices==1&&r.settingsNoticeRenderReady);
 settingsSaveNotice().recordFailure();r.pollSettingsSaveWarning();assert(r.updates==1);
 r.drawSuccess=false;r.render(RenderLock{});assert(r.notices==1&&!r.settingsNoticeRenderReady);
 for(int i=0;i<100;++i)r.pollSettingsSaveWarning();assert(r.updates==1);
 r.drawSuccess=true;r.render(RenderLock{});assert(r.notices==2&&r.settingsNoticeRenderReady);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'test.cpp').write_text(fixture + extract('void ReaderActivity::pollSettingsSaveWarning()') +
                                   extract('void ReaderActivity::render(RenderLock&&)') + body)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(root / 'src/activities/reader'), str(path / 'test.cpp'), '-o', str(path / 'test')],
                   check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Reader warning defers after failed renders and resumes after a page')
