"""Exercise the production image-cache serializer against every truncated write."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
start=source.index('bool ImageBlock::serialize(')
opening=source.index('{',start)
depth=0
for index in range(opening,len(source)):
    depth+=(source[index]=='{')-(source[index]=='}')
    if not depth:
        method=source[start:index+1]
        break
else:
    raise RuntimeError('image serializer not found')
code=r'''
#include <cassert>
#include <cstdint>
#include <string>
struct HalFile {size_t remaining=SIZE_MAX,bytes=0;size_t write(const uint8_t*,size_t n){size_t w=n<remaining?n:remaining;remaining-=w;bytes+=w;return w;}};
namespace serialization {
 template<class T>bool tryWritePod(HalFile& f,const T& t){return f.write(reinterpret_cast<const uint8_t*>(&t),sizeof(T))==sizeof(T);}
 bool tryWriteString(HalFile& f,const std::string& s){uint32_t n=s.size();return tryWritePod(f,n)&&(!n||f.write(reinterpret_cast<const uint8_t*>(s.data()),n)==n);}
}
struct ImageBlock {std::string imagePath="/cached.png",srcPath="/source.png";int16_t width=40,height=80;bool serialize(HalFile&);};
''' + method + r'''
int main(){ImageBlock image;HalFile good;assert(image.serialize(good));assert(good.bytes>0);
 for(size_t n=0;n<good.bytes;++n){HalFile f;f.remaining=n;assert(!image.serialize(f));}}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory);(path/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Image block short-write checks passed')
