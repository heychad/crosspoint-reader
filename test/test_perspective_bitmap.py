"""Check production perspective rendering against a destination-pixel oracle."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()


def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


helpers = (root / 'lib/GfxRenderer/BitmapHelpers.h').read_text()
gray = helpers[helpers.index('struct GrayPlanePixel'):helpers.index('\nenum class BmpRowOrder')]
program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#define LOG_ERR(...) ((void)0)
bool allocationFails=false;
size_t requested=0;
template<class T> std::unique_ptr<T> makeUniqueNoThrow(size_t count){
 requested=count;if(allocationFails)return {};return std::make_unique<T>(count);
}
enum class BmpReaderError {Ok,Failed};
struct Bitmap {
 int width=5,height=4,failAt=-1;bool topDown=false;mutable int row=0;
 int getWidth()const{return width;}int getHeight()const{return height;}
 int getRowBytes()const{return ((width+31)/32)*4;}
 bool isTopDown()const{return topDown;}
 int level(int x,int y)const{return (x+2*y)%4;}
 BmpReaderError readNextRow(uint8_t* out,uint8_t*)const{
  if(row==failAt)return BmpReaderError::Failed;
  assert(row<height);std::fill(out,out+(width+3)/4,0);
  const int y=topDown?row:height-1-row;
  for(int x=0;x<width;++x)out[x/4]|=level(x,y)<<(6-(x%4)*2);
  ++row;return BmpReaderError::Ok;
 }
};
struct Cache {bool scanning=false;bool isScanning()const{return scanning;}};
struct GfxRenderer {
 enum RenderMode {BW,GRAYSCALE_LSB,GRAYSCALE_MSB};
 int width=12,height=8;RenderMode renderMode=BW;bool absoluteGrayPlanes=false,inverted=false;
 int clipX=0,clipY=0,clipW=12,clipH=8;
 Cache cache;const Cache* fontCacheManager_=&cache;
 mutable std::vector<uint8_t> pixels=std::vector<uint8_t>(96,0);
 int getScreenWidth()const{return width;}int getScreenHeight()const{return height;}
 bool inside(int x,int y)const{return x>=0&&y>=0&&x<width&&y<height&&x>=clipX&&y>=clipY&&x<clipX+clipW&&y<clipY+clipH;}
 void drawPixel(int x,int y,bool black=true)const{assert(x>=0&&y>=0&&x<width&&y<height);if(inside(x,y))pixels[y*width+x]=black;}
 void preserveImagePolarity(int x,int y,int w,int h)const{
  if(!inverted||renderMode!=BW)return;
  for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)if(inside(xx,yy))pixels[yy*width+xx]^=1;
 }
 bool drawPerspectiveBitmap(const Bitmap&,int,int,int,int,int)const;
 void drawIcon(const uint8_t[],int,int,int,bool=true)const;
};
''' + gray + method('bool GfxRenderer::drawPerspectiveBitmap(') + '\n' + method('void GfxRenderer::drawIcon(') + r'''
int main(){
 for(bool portrait:{false,true})for(bool topDown:{false,true})for(int sourceW:{1,5,9})for(int sourceH:{1,4,13})
 for(int w:{1,3,9})for(int hl:{1,3,10})for(int hr:{1,5,9})for(bool clip:{false,true})
 for(int mode:{0,1,2})for(bool absolute:{false,true})for(bool inverted:{false,true})for(bool initial:{false,true}){
  GfxRenderer r;r.width=portrait?8:12;r.height=portrait?12:8;
  r.renderMode=static_cast<GfxRenderer::RenderMode>(mode);r.absoluteGrayPlanes=absolute;r.inverted=inverted;
  r.clipX=clip?2:0;r.clipY=clip?2:0;r.clipW=r.width-(clip?4:0);r.clipH=r.height-(clip?4:0);
  r.pixels.assign(96,initial);
  Bitmap b;b.width=sourceW;b.height=sourceH;b.topDown=topDown;
  const int x=-1,y=-2;
  assert(r.drawPerspectiveBitmap(b,x,y,w,hl,hr));
  assert(b.row==sourceH && requested==size_t((sourceW+3)/4+b.getRowBytes()));
  for(int yy=0;yy<r.height;++yy)for(int xx=0;xx<r.width;++xx){
   int expected=initial;const int dx=xx-x;
   if(r.inside(xx,yy)&&dx>=0&&dx<w){
    const int columnHeight=w==1?hl:hl+(hr-hl)*dx/(w-1);
    const int top=y+(std::max(hl,hr)-columnHeight)/2;
    if(yy>=top&&yy<top+columnHeight){
     const int sx=dx*sourceW/w,sy=((yy-top+1)*sourceH-1)/columnHeight;
     const int level=b.level(sx,sy);
     if(mode==0)expected=(level<3?true:initial)^inverted;
     else if(absolute)expected=!(level==3||level==(mode==2?2:1));
     else if((mode==1&&level==1)||(mode==2&&(level==1||level==2)))expected=0;
    }
   }
   assert(r.pixels[yy*r.width+xx]==expected);
  }
 }
 GfxRenderer r;Bitmap b;
 allocationFails=true;assert(!r.drawPerspectiveBitmap(b,0,0,4,6,3));assert(b.row==0);
 allocationFails=false;b.failAt=2;assert(!r.drawPerspectiveBitmap(b,0,0,4,6,3));assert(b.row==2);
 b={};r.cache.scanning=true;assert(!r.drawPerspectiveBitmap(b,0,0,4,6,3));assert(b.row==0);
 r.cache.scanning=false;assert(!r.drawPerspectiveBitmap(b,0,0,0,6,3));
 assert(!r.drawPerspectiveBitmap(b,0,0,4,0,3));assert(!r.drawPerspectiveBitmap(b,0,0,4,6,0));
 b.width=0;assert(!r.drawPerspectiveBitmap(b,0,0,4,6,3));
 // Existing black-icon calls and new white-icon calls share the same pixel positions.
 const uint8_t icon[]={0x5f,0xaf,0xdf,0xef};
 for(bool state:{true,false}){
  r.pixels.assign(96,!state);r.drawIcon(icon,2,1,4,state);
  for(int row=0;row<4;++row)for(int col=0;col<4;++col){
   const bool ink=(icon[row]&(0x80>>col))==0;
   assert(r.pixels[(1+col)*r.width+2+3-row]==(ink?state:!state));
  }
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Perspective pixels, gray planes, clipping, row order, OOM, I/O, and icon polarity passed')
