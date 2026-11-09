"""Run the production saved-progress reader against fault-injected files."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
stubs = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
inline std::vector<uint8_t> fakeBytes;
inline bool shortRead=false, canOpen=true;
struct HalFile {
 size_t fileSize()const{return fakeBytes.size();}
 int read(void* out,size_t size){size_t n=std::min(size,fakeBytes.size());if(shortRead && n>0)--n;memcpy(out,fakeBytes.data(),n);return static_cast<int>(n);}
};
struct FakeStorage {bool openFileForRead(const char*,const std::string&,HalFile&){return canOpen;}};
inline FakeStorage Storage;
namespace FsHelpers {
inline bool hasReflowableBookExtension(const std::string& p){return p=="test.epub";}
inline bool hasXtcExtension(const std::string& p){return p=="test.xtc";}
}
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... args){return std::make_unique<T>(static_cast<A&&>(args)...);}
struct Epub {
 Epub(const std::string&,const char*){} bool load(bool,bool){return true;}
 std::string getCachePath()const{return "cache";} int getSpineItemsCount()const{return 4;}
 unsigned getBookSize()const{return 100;} float calculateProgress(int,float f)const{return .25f+f/2;}
};
struct Xtc {
 Xtc(const std::string&,const char*){} bool load(){return true;}
 std::string getCachePath()const{return "cache";} unsigned getPageCount()const{return 10;}
 int calculateProgress(unsigned page)const{return static_cast<int>(page*10);}
};
#define LOG_ERR(...)
'''
main = r'''
#include "stub.h"
#include "BookProgress.h"
#include <cassert>
int main(){
 fakeBytes={1,0,1,0}; assert(loadBookProgress("test.epub")==25);
 fakeBytes={1,0,1,0,2,0}; assert(loadBookProgress("test.epub")==50);
 fakeBytes={1,0,1,0,2,0,0,0,0,0}; assert(loadBookProgress("test.epub")==50);
 fakeBytes.push_back(0);assert(loadBookProgress("test.epub")==-1);
 fakeBytes={1,0,1};assert(loadBookProgress("test.epub")==-1);
 fakeBytes={1,0,1,0,2,0};shortRead=true;assert(loadBookProgress("test.epub")==-1);shortRead=false;
 fakeBytes={4,0,0,0};assert(loadBookProgress("test.epub")==100);
 fakeBytes={5,0,0,0};assert(loadBookProgress("test.epub")==-1);
 fakeBytes={5,0,0,0};assert(loadBookProgress("test.xtc")==50);
 fakeBytes={10,0,0,0};assert(loadBookProgress("test.xtc")==100);
 fakeBytes={11,0,0,0};assert(loadBookProgress("test.xtc")==-1);
 fakeBytes={255,255,255,255};assert(loadBookProgress("test.xtc")==-1);
 fakeBytes={5,0,0,0,0};assert(loadBookProgress("test.xtc")==-1);
 fakeBytes={5,0,0,0};shortRead=true;assert(loadBookProgress("test.xtc")==-1);shortRead=false;
 canOpen=false;assert(loadBookProgress("test.xtc")==-1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / 'stub.h').write_text(stubs)
    for name in ['Epub.h','FsHelpers.h','HalStorage.h','Logging.h','Memory.h','Xtc.h']:
        (directory / name).write_text('#include "stub.h"\n')
    (directory / 'main.cpp').write_text(main)
    subprocess.run(['c++','-std=c++20','-I',str(directory),'-I',str(root / 'src/util'),str(root / 'src/util/BookProgress.cpp'),str(directory / 'main.cpp'),'-o',str(directory / 'test')],check=True)
    subprocess.run([str(directory / 'test')],check=True)
print('Progress reader rejects malformed and out-of-range records')
