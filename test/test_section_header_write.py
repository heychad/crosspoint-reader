"""Fault-test the production section-cache header writer."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'lib/Epub/Epub/Section.cpp').read_text()
start=source.index('bool Section::writeSectionFileHeader(')
opening=source.index('{',start)
depth=0
for index in range(opening,len(source)):
    depth+=(source[index]=='{')-(source[index]=='}')
    if not depth:
        method=source[start:index+1]
        break
else:
    raise RuntimeError('header writer not found')
constant=source[source.index('constexpr uint32_t HEADER_SIZE ='):source.index(';',source.index('constexpr uint32_t HEADER_SIZE ='))+1]
code=r'''
#include <cassert>
#include <cstdint>
#include <string>
#include "ReaderRenderSpec.h"
#define LOG_DBG(...) ((void)0)
constexpr uint8_t SECTION_FILE_VERSION=43,SECTION_FILE_INCOMPLETE_VERSION=0;
''' + constant + r'''
struct File {bool open=true;int calls=0,failAt=-1;std::string bytes;
 explicit operator bool()const{return open;}
 size_t write(const uint8_t* p,size_t n){if(calls++==failAt)return 0;bytes.append(reinterpret_cast<const char*>(p),n);return n;}};
namespace serialization {template<class T>bool tryWritePod(File& f,const T& v){return f.write(reinterpret_cast<const uint8_t*>(&v),sizeof(T))==sizeof(T);}}
struct Section {File file;uint16_t pageCount=0;bool writeSectionFileHeader(const ReaderRenderSpec&);};
''' + method + r'''
int main(){ReaderRenderSpec spec;Section good;assert(good.writeSectionFileHeader(spec));assert(good.file.bytes.size()==HEADER_SIZE && good.file.bytes[0]==0);
 for(int n=0;n<good.file.calls;++n){Section s;s.file.failAt=n;assert(!s.writeSectionFileHeader(spec));}
 Section closed;closed.file.open=false;assert(!closed.writeSectionFileHeader(spec));}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-I',str(root/'lib/Epub/Epub'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Section header short-write checks passed')
