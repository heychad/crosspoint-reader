"""Exercise the real Section page lookup against malformed cache offsets."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()
start = source.index('std::unique_ptr<Page> Section::loadPageAt(')
opening = source.index('{', start)
end, depth = opening + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
method = source[start:end]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
constexpr uint32_t HEADER_SIZE=32;
struct HalFile {
 std::vector<uint8_t> bytes;size_t pos=0;
 size_t size(){return bytes.size();}size_t position(){return pos;}bool seek(size_t p){if(p>bytes.size())return false;pos=p;return true;}
 size_t read(uint8_t* out,size_t n){if(pos>bytes.size() || n>bytes.size()-pos)return 0;memcpy(out,bytes.data()+pos,n);pos+=n;return n;}
};
namespace serialization {template<typename T>bool tryReadPod(HalFile& f,T& v){return f.read(reinterpret_cast<uint8_t*>(&v),sizeof(v))==sizeof(v);}}
struct StorageType {std::vector<uint8_t> bytes;bool openFileForRead(const char*,const std::string&,HalFile& f){f.bytes=bytes;return true;}} Storage;
struct Page {static int reads;uint32_t visibleTextOffset=0;static std::unique_ptr<Page> deserialize(HalFile&){++reads;return std::make_unique<Page>();}};
int Page::reads=0;
struct Section {std::string filePath="cache";bool partial_=false;uint16_t pageCount=1,partialPageCount_=0;std::unique_ptr<Page> loadPageAt(int)const;};
''' + method + r'''
void put(size_t at,uint32_t value){assert(at+4<=Storage.bytes.size());memcpy(Storage.bytes.data()+at,&value,4);}
void put16(size_t at,uint16_t value){assert(at+2<=Storage.bytes.size());memcpy(Storage.bytes.data()+at,&value,2);}
void reset(){Storage.bytes.assign(56,0);put(12,40);put(16,44);put(20,46);put(24,50);put(28,52);put(40,32);put16(46,1);put(52,123);Page::reads=0;}
int main(){Section s;
 reset();auto page=s.loadPageAt(0);assert(page && page->visibleTextOffset==123 && Page::reads==1);
 reset();assert(!s.loadPageAt(-1) && Page::reads==0);
 reset();Storage.bytes.resize(20);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put(12,31);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put(12,UINT32_MAX);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put(40,31);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put(40,40);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();assert(!s.loadPageAt(INT32_MAX) && Page::reads==0);
 reset();put(28,UINT32_MAX);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put(16,42);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put(20,45);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put(24,49);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put(28,51);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put16(44,1);assert(!s.loadPageAt(0) && Page::reads==0);
 reset();put16(46,0);assert(!s.loadPageAt(0) && Page::reads==0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, executable = Path(directory) / 'test.cpp', Path(directory) / 'test'
    cpp.write_text(code)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('Malformed section page offsets are rejected before page decoding')
