"""Exercise production sleep-image caching and render failures with checked handles."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()


def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)


program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
int yields=0;
void vTaskDelay(int){++yields;}
struct Node {
  std::vector<uint8_t> bytes=std::vector<uint8_t>(70000, 5);
  int width=20,height=32,bpp=24;
  bool valid=true,failRead=false,failSeek=false;
};
std::map<std::string,std::shared_ptr<Node>> files;
std::map<std::string,int> handles;
bool failClose=false,failRename=false,failConvert=false,failWriteOpen=false;
int conversions=0;
struct HalFile {
  std::shared_ptr<Node> node;
  std::string path;
  size_t cursor=0;
  ~HalFile(){close();}
  bool close(){
    if(!node)return true;
    --handles[path];node.reset();
    if(path.ends_with(".tmp")&&failClose){failClose=false;return false;}
    return true;
  }
  bool isOpen()const{return node!=nullptr;}
  bool seek(size_t at){if(!node||node->failSeek||at>node->bytes.size())return false;cursor=at;return true;}
  size_t size()const{return node?node->bytes.size():0;}
  int read(void* out,size_t count){
    if(!node||node->failRead)return 0;
    count=std::min(count,node->bytes.size()-cursor);
    std::memcpy(out,node->bytes.data()+cursor,count);cursor+=count;return count;
  }
};
struct Store {
  bool exists(const char* path){return files.count(path);}
  bool openFileForRead(const char*,const std::string& path,HalFile& file){
    file.close();if(!files.count(path))return false;
    assert(handles[path]==0);++handles[path];file.path=path;file.node=files[path];file.cursor=0;return true;
  }
  bool openFileForWrite(const char* tag,const std::string& path,HalFile& file){
    if(failWriteOpen)return false;
    files[path]=std::make_shared<Node>();return openFileForRead(tag,path,file);
  }
  bool rename(const char* from,const char* to){
    assert(handles[from]==0&&handles[to]==0);
    if(failRename)return false;
    files[to]=files.at(from);files.erase(from);return true;
  }
  void remove(const char* path){assert(handles[path]==0);files.erase(path);}
} Storage;
enum class BmpReaderError {Ok,Failed};
struct Bitmap {
  HalFile& file;
  Bitmap(HalFile& f,bool=false,bool=false):file(f){}
  BmpReaderError parseHeaders(){return file.node&&file.node->valid?BmpReaderError::Ok:BmpReaderError::Failed;}
  BmpReaderError rewindToData()const{return file.node&&!file.node->failSeek?BmpReaderError::Ok:BmpReaderError::Failed;}
  int getWidth()const{return file.node->width;}
  int getHeight()const{return file.node->height;}
  int getBpp()const{return file.node->bpp;}
  bool hasGreyscale()const{return getBpp()>1;}
};
struct BmpToBmpConverter {
  static bool bmpFileToBmpStream(HalFile& in,HalFile& out,int width,int height,bool,bool){
    assert(in.cursor==0);++conversions;
    if(failConvert)return false;
    out.node->width=width;out.node->height=height;out.node->bpp=2;return true;
  }
};
struct CrossPointSettings {
  enum SLEEP_SCREEN_COVER_MODE {FIT,CROP};
  enum SLEEP_SCREEN_COVER_FILTER {NO_FILTER,BLACK_AND_WHITE,INVERTED_BLACK_AND_WHITE};
};
struct {int sleepScreenCoverMode=CrossPointSettings::CROP,sleepScreenCoverFilter=CrossPointSettings::NO_FILTER;} SETTINGS;
struct HalDisplay {enum RefreshMode {HALF_REFRESH};enum class GrayscaleMode {Direct};};
struct Caps {bool enabled=true;bool supported()const{return enabled;}};
struct GfxRenderer {
  enum RenderMode {BW,GRAYSCALE_LSB,GRAYSCALE_MSB};
  int width=480,height=800,mode=BW,draws=0,displays=0,stored=0,restored=0;
  bool failCached=false,failDirect=false,failBase=false,saveReady=true,absolute=true;
  int failDrawAt=0;
  int getScreenWidth()const{return width;}
  int getScreenHeight()const{return height;}
  Caps grayscaleCapabilities(HalDisplay::GrayscaleMode)const{return{absolute};}
  bool storeBwBuffer(){++stored;return saveReady;}
  void restoreBwBuffer(){++restored;}
  void clearScreen(int=0){}
  bool drawBitmap(const Bitmap& bitmap,int,int,int,int,float,float,bool){
    ++draws;
    const bool cached=bitmap.file.path.starts_with("/.crosspoint/");
    if(cached)assert(handles["/sleep.bmp"]==0);
    return !(cached?failCached:failDirect)&&draws!=failDrawAt;
  }
  void displayBuffer(int){++displays;}
  void invertScreen(){}
  bool displayGrayscaleBase(HalDisplay::GrayscaleMode){++displays;return !failBase;}
  void displayGrayscaleBase(HalDisplay::RefreshMode){++displays;}
  void setRenderMode(RenderMode value){mode=value;}
  void copyGrayscaleLsbBuffers(){}
  void copyGrayscaleMsbBuffers(){}
  void displayGrayBuffer(){++displays;}
} renderer;
struct Placement {int x=0,y=0;float cropX=0,cropY=0;};
Placement calculateBitmapPlacement(int,int,const GfxRenderer&){return{};}
HalDisplay::GrayscaleMode sleepGrayscaleMode(const GfxRenderer&){return HalDisplay::GrayscaleMode::Direct;}
constexpr int kSleepClean=1;
void cleanBeforeAbsoluteGray(GfxRenderer&){}
bool useOriginalCoverThresholds(const GfxRenderer&){return false;}
bool beginOverlayGrayPlane(GfxRenderer&,const Bitmap*,GfxRenderer::RenderMode,bool){return true;}
struct SleepActivity {
  mutable const Bitmap* overlayCover=nullptr;
  mutable const Bitmap* overlayGrayCover=nullptr;
  void renderTransparentCustomSleepScreen()const{}
  bool renderCustomSleepBmp(const std::string&)const;
  bool renderBitmapSleepScreen(const Bitmap&,bool preserveBackground=false)const;
};
''' + method('class BwFrameKeeper') + ';\n' + method('bool sleepImageFingerprint(') + '\n' + method(
    'bool SleepActivity::renderCustomSleepBmp(') + '\n' + method(
    'bool SleepActivity::renderBitmapSleepScreen(') + r'''
void reset(){
  for(const auto& [_,count]:handles)assert(count==0);
  files.clear();handles.clear();files["/sleep.bmp"]=std::make_shared<Node>();
  renderer={};SETTINGS={};conversions=yields=0;
  failClose=failRename=failConvert=failWriteOpen=false;
}
std::string cachePath(){for(const auto& [path,_]:files)if(path.starts_with("/.crosspoint/")&&path.ends_with(".bmp"))return path;return{};}
int main(){
  reset();SleepActivity sleep;
  assert(sleep.renderCustomSleepBmp("/sleep.bmp"));
  assert(conversions==1&&yields>0);
  const auto first=cachePath();assert(!first.empty());
  assert(sleep.renderCustomSleepBmp("/sleep.bmp")&&conversions==1);
  files["/sleep.bmp"]->bytes.back()^=1;  // Same path, size, and timestamp.
  assert(sleep.renderCustomSleepBmp("/sleep.bmp")&&conversions==2);
  renderer.width=800;renderer.height=480;
  assert(sleep.renderCustomSleepBmp("/sleep.bmp")&&conversions==3);
  SETTINGS.sleepScreenCoverMode=CrossPointSettings::FIT;
  assert(sleep.renderCustomSleepBmp("/sleep.bmp")&&conversions==4);
  files[first]->valid=false;renderer.width=480;renderer.height=800;SETTINGS.sleepScreenCoverMode=CrossPointSettings::CROP;
  files["/sleep.bmp"]->bytes.back()^=1;
  assert(sleep.renderCustomSleepBmp("/sleep.bmp"));assert(!files.count(first));
  assert(sleep.renderCustomSleepBmp("/sleep.bmp")&&conversions==5);
  for(int failure=0;failure<5;++failure){
    reset();
    failConvert=failure==0;failClose=failure==1;failRename=failure==2;failWriteOpen=failure==3;renderer.failCached=failure==4;
    assert(sleep.renderCustomSleepBmp("/sleep.bmp"));  // Direct fallback remains readable.
    assert(cachePath().empty());
    for(const auto& [path,_]:files)assert(!path.ends_with(".tmp"));
  }
  reset();renderer.failCached=renderer.failDirect=true;
  assert(!sleep.renderCustomSleepBmp("/sleep.bmp")&&cachePath().empty());
  reset();files["/sleep.bmp"]->failRead=true;
  assert(!sleep.renderCustomSleepBmp("/sleep.bmp")&&conversions==0);
  reset();files["/sleep.bmp"]->failSeek=true;
  assert(!sleep.renderCustomSleepBmp("/sleep.bmp")&&conversions==0);
  reset();files["/sleep.bmp"]->width=480;files["/sleep.bmp"]->height=800;files["/sleep.bmp"]->bpp=1;
  assert(sleep.renderCustomSleepBmp("/sleep.bmp")&&conversions==0&&cachePath().empty());
  for(const int failedDraw:{2,3}){
    reset();renderer.failDrawAt=failedDraw;
    HalFile file;assert(Storage.openFileForRead("TEST","/sleep.bmp",file));Bitmap bitmap(file);
    assert(!sleep.renderBitmapSleepScreen(bitmap)&&renderer.mode==GfxRenderer::BW);
  }
  reset();renderer.failBase=true;
  assert(!sleep.renderCustomSleepBmp("/sleep.bmp"));
  reset();renderer.absolute=false;renderer.saveReady=false;
  assert(!sleep.renderCustomSleepBmp("/sleep.bmp"));
  assert(renderer.restored==0);
  reset();renderer.absolute=false;
  assert(sleep.renderCustomSleepBmp("/sleep.bmp"));
  assert(renderer.stored==1&&renderer.restored==1&&renderer.mode==GfxRenderer::BW);
  reset();
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Sleep image identity, bounds, handle ownership, fallback, and render failures passed')
