"""Execute the production XTC buffer path against 1/2-bit data, failures, and display output."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/activities/reader/XtcReaderActivity.cpp').read_text()
def method(signature):
 start=source.index(signature);opening=source.index('{',start);depth=0
 for end in range(opening,len(source)):
  depth+=(source[end]=='{')-(source[end]=='}')
  if depth==0:return source[start:end+1]
 raise ValueError(signature)
program=r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define tr(x) "error"
enum{UI_12_FONT_ID};struct EpdFontFamily{enum{BOLD};};
struct HalDisplay{enum{HALF_REFRESH,FAST_REFRESH};enum class GrayscaleBase{Combined,Other};};
struct Renderer{
 struct Capability{HalDisplay::GrayscaleBase base=HalDisplay::GrayscaleBase::Other;};
 int width=8,height=9,displays=0,marked=0;std::vector<bool>pixels=std::vector<bool>(72,false);
 void clearScreen(int fill=255){pixels.assign(72,fill==0);}void drawPixel(int x,int y,bool black){assert(x>=0&&x<width&&y>=0&&y<height);pixels[y*width+x]=black;}
 void drawCenteredText(int,int,const char*,bool,int){}void displayBuffer(int=0){++displays;}
 void displayGrayscaleBase(int){++displays;}Capability grayscaleCapabilities()const{return{};}
 void preconditionGrayscale(){}void copyGrayscaleLsbBuffers(){}void copyGrayscaleMsbBuffers(){}
 void displayGrayBuffer(){++displays;}void cleanupGrayscaleWithFrameBuffer(){}
};
struct CrossPointSettings{enum class XTC_STATUS_BAR_MODE{XTC_STATUS_BAR_TOP,XTC_STATUS_BAR_BOTTOM};};
struct Settings{struct Spec{CrossPointSettings::XTC_STATUS_BAR_MODE xtcMode=CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM;};
 Spec statusBarSpec()const{return{};}int getRefreshFrequency()const{return 10;}}SETTINGS;
namespace ReaderUtils{void displayWithRefreshCycle(Renderer&r,int&count){++r.displays;--count;}}
namespace xtc{const char*errorToString(int){return "read failed";}}
bool allocationAllowed=true;
template<class T>auto makeUniqueNoThrow(size_t n){using Element=std::remove_extent_t<T>;return allocationAllowed?std::make_unique<Element[]>(n):nullptr;}
struct Xtc{
 int depth=1,width=8,height=9;bool fail=false,shortRead=false;int loads=0;std::vector<uint8_t>data;
 int getBitDepth()const{return depth;}int getPageWidth()const{return width;}int getPageHeight()const{return height;}
 uint32_t getPageCount()const{return 4;}int getLastError()const{return 0;}
 size_t loadPage(uint32_t page,uint8_t*out,size_t size){assert(page==2);++loads;if(fail)return 0;
  assert(size==data.size());std::copy(data.begin(),data.end(),out);return shortRead?size-1:size;}
};
struct XtcReaderActivity{
 enum class PageRenderTarget{Display,Buffer};enum class StatusBarOverlayPosition{Top,Bottom};
 std::unique_ptr<Xtc>xtc=std::make_unique<Xtc>();uint32_t currentPage=2;int pagesUntilFullRefresh=7,status=0,marks=0;Renderer renderer;
 void renderStatusBarOverlay(Renderer&,StatusBarOverlayPosition){++status;}void markPageRendered(){++marks;}
 bool renderPage(PageRenderTarget target=PageRenderTarget::Display);
};
''' + method('bool XtcReaderActivity::renderPage(') + r'''
int main(){
 using Target=XtcReaderActivity::PageRenderTarget;
 for(int depth:{1,2})for(int display:{0,1}){
  XtcReaderActivity reader;reader.xtc->depth=depth;
  const int colBytes=2,planeBytes=16;reader.xtc->data.assign(depth==1?9:32,depth==1?255:0);
  std::vector<bool>expected(72,false);
  for(int y=0;y<9;++y)for(int x=0;x<8;++x){
   const int value=(x+2*y)%4;expected[y*8+x]=depth==1?value%2==1:value>=1;
   if(depth==1){if(expected[y*8+x])reader.xtc->data[y]&=~(1<<(7-x));}
   else{const int offset=(7-x)*colBytes+y/8,bit=7-y%8;
    if(value&2)reader.xtc->data[offset]|=1<<bit;if(value&1)reader.xtc->data[planeBytes+offset]|=1<<bit;}
  }
  assert(reader.renderPage(display?Target::Display:Target::Buffer));
  assert(reader.renderer.pixels==expected&&reader.currentPage==2&&reader.xtc->loads==1);
  if(display){assert(reader.renderer.displays>0&&reader.marks==1&&reader.pagesUntilFullRefresh==6);}
  else{assert(reader.renderer.displays==0&&reader.marks==0&&reader.pagesUntilFullRefresh==7);}
  assert(reader.status==(depth==1?1:0));
 }
 for(int failure=0;failure<6;++failure){
  XtcReaderActivity reader;reader.xtc->data.assign(9,255);
  if(failure==0)reader.xtc.reset();if(failure==1)reader.currentPage=4;
  if(failure==2)allocationAllowed=false;if(failure==3)reader.xtc->fail=true;
  if(failure==4)reader.xtc->shortRead=true;if(failure==5)reader.xtc->depth=3;
  assert(!reader.renderPage(Target::Buffer));assert(reader.renderer.displays==0&&reader.marks==0&&reader.pagesUntilFullRefresh==7);
  allocationAllowed=true;
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
 path=Path(directory);(path/'test.cpp').write_text(program)
 subprocess.run(['c++','-std=c++20',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
print('XTC sleep rendering preserves padded bitplanes, current page, refresh count, and display ownership')
