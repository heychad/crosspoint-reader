"""Exercise the production bounded chapter lookup against malformed caches."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'lib/Epub/Epub/BookMetadataCache.cpp').read_text()
start = source.index('bool BookMetadataCache::copySpineTocTitle(')
end = source.index('\nBookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry', start)
method = source[start:end]
program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
struct File {
 std::vector<uint8_t> bytes;size_t cursor=0;bool failSeek=false,shortRead=false;
 uint64_t fileSize64(){return bytes.size();}
 bool seek64(uint64_t pos){if(failSeek || pos>bytes.size())return false;cursor=static_cast<size_t>(pos);return true;}
 int read(void* out,size_t count){size_t n=std::min(count,bytes.size()-cursor);if(shortRead && n)--n;memcpy(out,bytes.data()+cursor,n);cursor+=n;return int(n);}
};
int utf8SafeTruncateBuffer(const char* b,int n){if(!n)return 0;int p=n-1;while(p>0&&(static_cast<unsigned char>(b[p])&0xc0)==0x80)--p;unsigned char c=b[p];int len=c<128?1:c<224?2:c<240?3:4;return n-p<len?p:n;}
struct BookMetadataCache {bool loaded=true;uint16_t spineCount=1,tocCount=1;uint32_t lutOffset=0;File bookFile;bool copySpineTocTitle(int,char*,size_t);};
''' + method + r'''
void put32(std::vector<uint8_t>& v,int p,uint32_t x){for(int i=0;i<4;++i)v[p+i]=uint8_t(x>>(8*i));}
int main(){
 BookMetadataCache cache;auto& b=cache.bookFile.bytes;b.resize(128);
 put32(b,0,32);put32(b,4,64);put32(b,32,4);b[44]=0;b[45]=0;
 const char title[]="ab\xe2\x82\xacz";put32(b,64,sizeof(title)-1);memcpy(b.data()+68,title,sizeof(title)-1);
 char out[16]{};assert(cache.copySpineTocTitle(0,out,sizeof(out)) && strcmp(out,title)==0);
 char shortOut[4]{};assert(cache.copySpineTocTitle(0,shortOut,sizeof(shortOut)) && strcmp(shortOut,"ab")==0);
 const char multibyte[]="\xe2\x82\xac";put32(b,64,sizeof(multibyte)-1);memcpy(b.data()+68,multibyte,sizeof(multibyte)-1);
 char tiny[3]{};assert(!cache.copySpineTocTitle(0,tiny,sizeof(tiny)) && tiny[0]==0);
 put32(b,64,sizeof(title)-1);memcpy(b.data()+68,title,sizeof(title)-1);
 out[0]='X';assert(!cache.copySpineTocTitle(1,out,sizeof(out)) && out[0]==0);
 out[0]='X';assert(!cache.copySpineTocTitle(0,out,1) && out[0]==0);
 put32(b,64,0xffffffffu);out[0]='X';assert(!cache.copySpineTocTitle(0,out,sizeof(out)) && out[0]==0);
 put32(b,64,sizeof(title)-1);put32(b,32,0xffffffffu);assert(!cache.copySpineTocTitle(0,out,sizeof(out)));
 put32(b,32,4);cache.bookFile.failSeek=true;assert(!cache.copySpineTocTitle(0,out,sizeof(out)));
 cache.bookFile.failSeek=false;cache.bookFile.shortRead=true;assert(!cache.copySpineTocTitle(0,out,sizeof(out)));
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Bounded chapter lookup rejects truncated and invalid cache data')
