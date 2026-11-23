"""Exercise the real diagnostics activity with deterministic HAL samples."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / "src/activities/settings/DeviceDiagnosticsActivity.cpp"
keys = sorted(set(re.findall(r"STR_[A-Z_]+", source.read_text())))
english = (root / 'lib/I18n/translations/english.yaml').read_text()
timing_format = re.search(r'^STR_EPUB_TIMING_VALUE_FORMAT: "([^"]+)"$', english, re.M).group(1)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "activities").mkdir()
    (path / "components").mkdir()
    (path / "I18n.h").write_text(
        "#pragma once\nenum class StrId {" + ",".join(keys) + "};\n"
        + "struct Lang{const char*get(StrId i)const{static const char*labels[]={"
        + ",".join('"' + (timing_format if key == 'STR_EPUB_TIMING_VALUE_FORMAT' else key) + '"' for key in keys)
        + "};return labels[static_cast<unsigned>(i)];}};inline Lang I18N;\n"
        + "#define tr(x) I18N.get(StrId::x)\n"
    )
    (path / "HalMemory.h").write_text(r'''
#pragma once
#include <cstddef>
struct HalMemory {
 struct HeapStats {size_t freeBytes,totalBytes,minFreeBytes,largestBlockBytes;};
 static inline int calls=0;
 static HeapStats getInternalHeap(){++calls;return {100u+static_cast<size_t>(calls),400,50,70};}
 static HeapStats getPsramHeap(){return {500,1000,200,300};}
};
''')
    (path / "activities/UiListActivity.h").write_text(r'''
#pragma once
#include <cstdint>
struct GfxRenderer{};struct MappedInputManager{};
namespace freeink::ui {
struct Insets {int16_t a,b,c,d;};
struct ListItem {const char*label=nullptr;const char*value=nullptr;int16_t actionValue=0;};
struct Text {int maxLines=0;};
struct ListProps {ListItem*items=nullptr;int count=0,action=0,inputMask=0,valueInset=0;Text labelText;};
constexpr int InputTouch=1;
}
struct UiScreen {struct Theme{freeink::ui::Text smallText;};int seen=0;
 void setContentMarginFromScreen(freeink::ui::Insets){} void spacer(int16_t){}
 Theme theme(){return{};}void list(freeink::ui::ListProps p){seen=p.count;}
};
inline int locks=0;
struct RenderLock {template<class T>RenderLock(T&){++locks;}~RenderLock(){--locks;}};
class UiListActivity {public:
 UiListActivity(const char*,GfxRenderer&,MappedInputManager&){} virtual ~UiListActivity()=default;
 virtual void onEnter(){} virtual void render(RenderLock&&){++renders;} virtual int listCount()const=0; virtual void buildScreen(UiScreen&)=0;
 virtual void activateIndex(int)=0; virtual const char*headerTitle()const=0;
 struct App {int clears=0;void clearTapFlash(){++clears;}} app;
 bool updated=false;int renders=0;void requestUpdate(){updated=true;}
 void syncListViewport(UiScreen&,freeink::ui::ListProps&){}
 static constexpr int ACTION_ROW=1;
};
''')
    (path / "components/UITheme.h").write_text(r'''
#pragma once
struct UITheme {struct Metrics {int topPadding=1,headerHeight=1,buttonHintsHeight=1,verticalSpacing=1;};
 static UITheme&getInstance(){static UITheme theme;return theme;}Metrics getMetrics()const{return{};}};
''')
    harness = r'''
#include <cassert>
#include <string>
#include <HalMemory.h>
#include "activities/reader/EpubRenderTiming.h"
#define private public
#include "activities/settings/DeviceDiagnosticsActivity.h"
#undef private
int main(){
 using epubtiming::PageKind;
 auto& timing=epubtiming::recorder;
 GfxRenderer renderer;MappedInputManager input;DeviceDiagnosticsActivity screen(renderer,input);
 {RenderLock lock(screen);screen.render(std::move(lock));}assert(screen.renders==0);
 timing.record(PageKind::Text,5);
 screen.onEnter();assert(HalMemory::calls==1 && screen.listCount()==11);
 {RenderLock lock(screen);screen.render(std::move(lock));}assert(screen.renders==1);
 assert(std::string(screen.rows[0].label)=="STR_DIAG_INTERNAL_FREE");
 assert(std::string(screen.rows[0].value)=="101 B");
 assert(std::string(screen.rows[1].value)=="70 B");
 assert(std::string(screen.rows[2].value)=="50 B");
 assert(std::string(screen.rows[3].value)=="500 B");
 assert(std::string(screen.rows[4].value)=="300 B");
 assert(std::string(screen.rows[5].value)=="200 B");
 assert(screen.rows[6].value==nullptr);
 assert(std::string(screen.rows[7].value)=="STR_DISABLED");
 assert(std::string(screen.rows[8].value)=="-");
 screen.activateIndex(1);assert(HalMemory::calls==1 && !screen.updated);
 screen.activateIndex(6);assert(HalMemory::calls==2 && screen.updated && locks==0);
 assert(screen.app.clears==1 && std::string(screen.rows[0].value)=="102 B");
 screen.activateIndex(7);assert(timing.isRecording());
 assert(std::string(screen.rows[7].value)=="STR_ENABLED");
 timing.record(PageKind::Text,900);
 for(unsigned i=1;i<=16;i++)timing.record(PageKind::Text,i*10);
 timing.record(PageKind::Illustrated,300);
 timing.record(PageKind::Indexing,0);
 screen.activateIndex(6);
 assert(std::string(screen.rows[8].value)=="85 / 160 ms (16)");
 assert(std::string(screen.rows[9].value)=="300 / 300 ms (1)");
 assert(std::string(screen.rows[10].value)=="0 / 0 ms (1)");
 screen.activateIndex(7);assert(!timing.isRecording());
 timing.record(PageKind::Illustrated,600);
 screen.activateIndex(6);
 assert(std::string(screen.rows[9].value)=="300 / 300 ms (1)");
 screen.activateIndex(7);assert(timing.isRecording());
 assert(std::string(screen.rows[8].value)=="-" && std::string(screen.rows[9].value)=="-");
 UiScreen ui;screen.buildScreen(ui);assert(ui.seen==11);
}
'''
    (path / "test.cpp").write_text(harness)
    subprocess.run(["c++", "-std=c++20", "-DFREEINK_DEVICE_X4PRO=1", "-I", str(path), "-I", str(root / "src"),
                    str(source), str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Diagnostics refreshes memory and records bounded EPUB timing samples on request")
