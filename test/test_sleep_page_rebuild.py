"""Exercise the production sleep reconstruction scheduler and local render recovery."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = source.index('std::unique_ptr<Page> EpubReaderActivity::rebuildPageForSleep(')
opening = source.index('{', start)
depth = 0
for end in range(opening, len(source)):
    depth += (source[end] == '{') - (source[end] == '}')
    if depth == 0:
        method = source[start:end + 1]
        break

program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "personalization/EpubRenderRecovery.h"
#include "activities/reader/ChapterPosition.h"
struct Page {int index;};
struct Epub {int getSpineItemsCount()const{return 2;}};
struct Heap {size_t freeBytes=128*1024,largestBlockBytes=64*1024;} heap;
struct HalMemory {static Heap getDefaultHeap(){return heap;}};
struct GfxRenderer {
 int loans=0;
 struct FrameBufferLoan {GfxRenderer&r;FrameBufferLoan(GfxRenderer&r):r(r){++r.loans;}~FrameBufferLoan(){--r.loans;}};
};
struct Config {
 bool loaded=false,partial=false,failPage=false,lowDuringBuild=false;
 int loadedPages=0,totalPages=20,step=2;
 EpubBuildFailure failure=EpubBuildFailure::None,chunkFailure=EpubBuildFailure::None;
 int recoverBelow=-1;
} config;
int created=0,live=0,builders=0,suspended=0,steps=0,starts=0,loaded=0,rooms=0,yields=0;
bool allocation=true,room=true;
std::vector<ReaderRenderSpec> attempts;
void vTaskDelay(int){++yields;}
struct Section {
 GfxRenderer*renderer=nullptr;
 bool original=true,building=false,partial=false,complete=false;
 int currentPage=0,pageCount=0,builtPages=0;
 EpubBuildFailure failure=EpubBuildFailure::None;
 Section()=default;
 Section(const std::shared_ptr<Epub>&,int,GfxRenderer&r):renderer(&r),original(false){++created;++live;}
 ~Section(){if(building){--builders;++suspended;}if(!original)--live;}
 bool isBuilding()const{return building;}
 bool isPartial()const{return partial;}
 bool isBuildComplete()const{return complete;}
 int estimatedTotalPages()const{return pageCount;}
 EpubBuildFailure lastBuildFailure()const{return failure;}
 bool loadSectionFile(const ReaderRenderSpec&){++loaded;pageCount=config.loaded?config.loadedPages:0;partial=config.partial;return config.loaded;}
 bool startBuild(const ReaderRenderSpec&spec){
   assert(renderer->loans==1&&builders==0);++starts;attempts.push_back(spec);
   if(config.failure!=EpubBuildFailure::None){failure=config.failure;return false;}
   const int profile=spec.embeddedStyle?static_cast<int>(spec.renderMode):3;
   if(profile<config.recoverBelow){failure=EpubBuildFailure::OutOfMemory;return false;}
   building=true;++builders;return true;
 }
 bool buildSomeMore(int){
   assert(renderer->loans==0&&building);++steps;
   if(config.chunkFailure!=EpubBuildFailure::None){failure=config.chunkFailure;building=false;--builders;return false;}
   builtPages=std::min(config.totalPages,builtPages+config.step);
   pageCount=std::max(pageCount,builtPages);
   if(builtPages==config.totalPages){complete=true;building=false;partial=false;--builders;}
   if(config.lowDuringBuild)heap.freeBytes=1;
   return true;
 }
 bool buildReachedVisibleTextOffset(uint32_t offset)const{return builtPages>0&&offset<=uint32_t((builtPages-1)*100);}
 std::optional<uint32_t>getVisibleTextOffsetForPage(uint16_t page)const{
   return page<pageCount?std::optional<uint32_t>(page*100):std::nullopt;
 }
 std::optional<uint16_t>getPageForVisibleTextOffset(uint32_t offset)const{
   return pageCount>0?std::optional<uint16_t>(std::min<uint32_t>(offset/100,pageCount-1)):std::nullopt;
 }
 std::unique_ptr<Page>loadPage(int page){
   assert(page>=0&&page<pageCount);
   return config.failPage?nullptr:std::make_unique<Page>(Page{page});
 }
};
template<class T,class...Args>std::unique_ptr<T>makeUniqueNoThrow(Args&&...args){
  return allocation?std::make_unique<T>(std::forward<Args>(args)...):nullptr;
}
struct Settings {
 ReaderRenderSpec readerRenderSpec(uint16_t width,uint16_t height)const{
   ReaderRenderSpec spec;spec.viewportWidth=width;spec.viewportHeight=height;return spec;
 }
} SETTINGS;
struct EpubReaderActivity {
 std::shared_ptr<Epub>epub=std::make_shared<Epub>();
 std::unique_ptr<Section>section;
 GfxRenderer renderer;
 EpubRenderRecovery renderRecovery;
 int currentSpineIndex=0,visibleOffsetSpine=-1,cachedSpineIndex=0;
 int nextPageNumber=4,cachedChapterTotalPageCount=20;
 std::optional<uint32_t>currentPageVisibleOffset,cachedVisibleTextOffset;
 static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP=32*1024;
 static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC=16*1024;
 static constexpr int BUILD_PAGES_PER_CHUNK=8;
 ChapterPosition chapterPosition()const{return section?ChapterPosition{section->currentPage,section->pageCount}:ChapterPosition{nextPageNumber,cachedChapterTotalPageCount};}
 bool makeRoomForChapterBuild(){++rooms;return room;}
 std::unique_ptr<Page>rebuildPageForSleep(uint16_t,uint16_t);
};
''' + method + r'''
void reset(){assert(live==0&&builders==0);config={};heap={};created=starts=steps=loaded=rooms=yields=suspended=0;allocation=room=true;attempts.clear();}
void unchanged(const EpubReaderActivity&r){
 assert(r.nextPageNumber==4&&r.cachedChapterTotalPageCount==20&&r.currentSpineIndex==0);
 assert(r.renderRecovery.spec(SETTINGS.readerRenderSpec(400,700),0).renderMode==EpubRenderMode::Default);
 assert(r.renderer.loans==0&&live==0&&builders==0);
}
int main(){
 reset();EpubReaderActivity reader;
 reader.cachedVisibleTextOffset=850;
 auto page=reader.rebuildPageForSleep(400,700);
 assert(page&&page->index==8&&steps==5&&suspended==1&&yields==5);
 unchanged(reader);
 assert(reader.cachedVisibleTextOffset==850);
 reset();config.loaded=true;config.loadedPages=20;
 reader.cachedVisibleTextOffset.reset();
 page=reader.rebuildPageForSleep(400,700);
 assert(page&&page->index==4&&starts==0&&rooms==1);unchanged(reader);
 for(const uint32_t offset:{250,850}){
   reset();config.loaded=config.partial=true;config.loadedPages=5;
   reader.cachedVisibleTextOffset=offset;
   page=reader.rebuildPageForSleep(400,700);
   assert(page&&page->index==int(offset/100));
   assert(starts==(offset==250?0:1));unchanged(reader);
 }
 reset();reader.cachedVisibleTextOffset.reset();
 page=reader.rebuildPageForSleep(400,700);
 assert(page&&page->index==4&&steps==3&&suspended==1);unchanged(reader);
 reset();config.totalPages=2;
 page=reader.rebuildPageForSleep(400,700);
 assert(page&&page->index==1&&suspended==0);unchanged(reader);
 reset();reader.section=std::make_unique<Section>();reader.section->currentPage=3;reader.section->pageCount=10;
 page=reader.rebuildPageForSleep(400,700);
 assert(page&&page->index==3&&reader.section->currentPage==3&&reader.section->pageCount==10);unchanged(reader);
 reset();reader.section->building=true;
 assert(!reader.rebuildPageForSleep(400,700)&&created==0);reader.section->building=false;
 reader.section.reset();
 for(const Heap budget:{Heap{1024,65536},Heap{65536,1024}}){
   reset();heap=budget;
   assert(!reader.rebuildPageForSleep(400,700)&&starts==0);unchanged(reader);
 }
 reset();config.lowDuringBuild=true;
 assert(!reader.rebuildPageForSleep(400,700)&&steps==1&&suspended==1);unchanged(reader);
 reset();room=false;
 assert(!reader.rebuildPageForSleep(400,700)&&starts==0);unchanged(reader);
 reset();allocation=false;
 assert(!reader.rebuildPageForSleep(400,700)&&created==0);unchanged(reader);
 reset();config.recoverBelow=3;
 page=reader.rebuildPageForSleep(400,700);
 assert(page&&attempts.size()==4&&!attempts.back().embeddedStyle);unchanged(reader);
 reset();config.failure=EpubBuildFailure::InputOutput;
 assert(!reader.rebuildPageForSleep(400,700)&&starts==1);unchanged(reader);
 reset();config.failure=EpubBuildFailure::OutOfMemory;
 assert(!reader.rebuildPageForSleep(400,700)&&starts==4);unchanged(reader);
 reset();config.chunkFailure=EpubBuildFailure::InputOutput;
 assert(!reader.rebuildPageForSleep(400,700)&&starts==1&&steps==1&&suspended==0);unchanged(reader);
 reset();config.chunkFailure=EpubBuildFailure::OutOfMemory;
 assert(!reader.rebuildPageForSleep(400,700)&&starts==4&&steps==4&&suspended==0);unchanged(reader);
 reset();config.failPage=true;
 assert(!reader.rebuildPageForSleep(400,700)&&suspended==1);unchanged(reader);
 reset();reader.currentSpineIndex=2;
 assert(!reader.rebuildPageForSleep(400,700)&&created==0);reader.currentSpineIndex=0;
 reader.nextPageNumber=-1;
 assert(!reader.rebuildPageForSleep(400,700)&&created==0);reader.nextPageNumber=20;
 assert(!reader.rebuildPageForSleep(400,700)&&created==0);reader.nextPageNumber=4;
 reader.epub.reset();assert(!reader.rebuildPageForSleep(400,700)&&created==0);
 reset();
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(root / 'src'), '-I', str(root / 'lib/Epub'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Sleep rebuild targeting, partial caches, recovery, ownership, and unchanged reader state passed')
