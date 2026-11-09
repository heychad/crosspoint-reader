"""Exercise the production section header writer and loader with each profile."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()
legacy_source = subprocess.check_output(['git', '-C', str(root), 'show',
    '965594ea161a83a708efc155117387658a503ce7:lib/Epub/Epub/Section.cpp'], text=True)
stock_source = subprocess.check_output(['git', '-C', str(root), 'show',
    '42d58daaabcf469a2e5e1ba871714fb9dcf51d29:lib/Epub/Epub/Section.cpp'], text=True)


def method(signature, text=source):
    start = text.index(signature)
    position = text.index('{', start)
    depth = 0
    for i in range(position, len(text)):
        depth += (text[i] == '{') - (text[i] == '}')
        if not depth:
            return text[start:i + 1]
    raise AssertionError(signature)


header = re.search(r'constexpr uint32_t HEADER_SIZE\s*=\s*.*?;', source, re.S).group()
version = int(re.search(r'constexpr uint8_t SECTION_FILE_VERSION = (\d+);', source).group(1))
assert version == 78
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "Epub/ReaderRenderSpec.h"
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
constexpr uint8_t SECTION_FILE_VERSION = 78;
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = 0xFE - (SECTION_FILE_VERSION - 28);
struct File {
  std::vector<uint8_t> bytes; size_t pos = 0; size_t maxBytes = SIZE_MAX;
  explicit operator bool() const { return true; }
  size_t write(const uint8_t* in, size_t n) {if (n > maxBytes - bytes.size()) return 0; bytes.insert(bytes.end(), in, in + n); return n;}
  size_t read(uint8_t* out, size_t n) {if(n>bytes.size()-pos)return 0;memcpy(out,bytes.data()+pos,n);pos+=n;return n;}
  bool seek(size_t next) {if(next>bytes.size())return false;pos=next;return true;}
  size_t size() const {return bytes.size();}
  void close() {}
};
namespace serialization {
template<class T> void writePod(File& f, const T& value) {f.write(reinterpret_cast<const uint8_t*>(&value), sizeof value);}
template<class T> bool tryWritePod(File& f, const T& value) {return f.write(reinterpret_cast<const uint8_t*>(&value), sizeof value)==sizeof value;}
template<class T> void readPod(File& f, T& value) {f.read(reinterpret_cast<uint8_t*>(&value), sizeof value);}
template<class T> bool tryReadPod(File& f, T& value) {return f.read(reinterpret_cast<uint8_t*>(&value), sizeof value)==sizeof value;}
}
struct HalStorage {enum class PathStatus{Missing,Present,Failed};};
struct StorageType {
  std::vector<uint8_t> saved,alternative;bool canonical=true,side=false;int sideOpens=0;
  bool openFileForRead(const char*, const std::string& path, File& f) {
    const bool other=path.ends_with(".prefetch");if(other)++sideOpens;
    if(other ? !side : !canonical)return false;f.bytes=other?alternative:saved;f.pos=0;return true;}
  HalStorage::PathStatus lookupPath(const char*) {return canonical?HalStorage::PathStatus::Present:HalStorage::PathStatus::Missing;}
} Storage;
struct Section {
  File file; std::string filePath,readFilePath_; uint16_t pageCount=2, partialPageCount_=0;
  uint32_t partialBytesConsumed_=0, partialTotalBytes_=0; bool partial_=false, cleared=false;
  bool writeSectionFileHeader(const ReaderRenderSpec&);
  bool loadSectionFile(const ReaderRenderSpec&);
  std::string speculativeFilePath()const{return filePath+".prefetch";}
  bool clearCache() {cleared=true; return true;}
};
struct LegacySection {
  File file;std::string filePath;uint16_t pageCount=2,partialPageCount_=0;
  uint32_t partialBytesConsumed_=0,partialTotalBytes_=0;bool partial_=false,cleared=false;
  bool loadSectionFile(const ReaderRenderSpec&);bool clearCache(){cleared=true;return true;}
};
struct StockSection {
  File file;std::string filePath;uint16_t pageCount=2,partialPageCount_=0;
  uint32_t partialBytesConsumed_=0,partialTotalBytes_=0;bool partial_=false,cleared=false;
  bool loadSectionFile(const ReaderRenderSpec&);bool clearCache(){cleared=true;return true;}
};
''' + header + '\n' + method('bool Section::writeSectionFileHeader(') + '\n' + method('bool Section::loadSectionFile(') + '\n' + method('bool Section::loadSectionFile(', legacy_source).replace('Section::loadSectionFile(', 'LegacySection::loadSectionFile(', 1) + '\n' + method('bool Section::loadSectionFile(', stock_source).replace('Section::loadSectionFile(', 'StockSection::loadSectionFile(', 1) + r'''
int main() {
  ReaderRenderSpec original; original.viewportWidth=480;original.viewportHeight=800;
  Section shortWrite;shortWrite.file.maxBytes=3;assert(!shortWrite.writeSectionFileHeader(original));
  assert(!shortWrite.file.bytes.empty() && shortWrite.file.bytes[0]==SECTION_FILE_INCOMPLETE_VERSION);
  auto signature=readerRenderSpecSignature(original);
  for (auto mode : {EpubRenderMode::Default, EpubRenderMode::Balanced, EpubRenderMode::Light}) {
    ReaderRenderSpec spec=original;spec.renderMode=mode;Section written;assert(written.writeSectionFileHeader(spec));
    assert(written.file.bytes.size()==HEADER_SIZE);Storage.saved=written.file.bytes;
    Storage.saved[0]=SECTION_FILE_VERSION;
    const auto complete=Storage.saved;
    for(size_t n=0;n<HEADER_SIZE-sizeof(uint32_t)*5;++n){
      Storage.saved.assign(complete.begin(),complete.begin()+n);Section truncated;
      assert(!truncated.loadSectionFile(spec) && truncated.cleared);
    }
    Storage.saved=complete;
    Storage.canonical=false;Storage.side=true;Storage.alternative=complete;Storage.sideOpens=0;
    Section alternate;assert(!alternate.loadSectionFile(spec) && !alternate.cleared && Storage.sideOpens==0);
    Storage.alternative.clear();Section incomplete;assert(!incomplete.loadSectionFile(spec) && !incomplete.cleared);
    Storage.sideOpens=0;LegacySection legacy;assert(!legacy.loadSectionFile(spec));
    assert(Storage.sideOpens==0 && !legacy.cleared);
    StockSection stock;assert(!stock.loadSectionFile(spec));assert(Storage.sideOpens==0 && !stock.cleared);
    Storage.canonical=true;Storage.side=false;Storage.saved=complete;
    Section match;assert(match.loadSectionFile(spec));assert(match.pageCount==2);assert(!match.cleared);
    ReaderRenderSpec other=spec;other.renderMode=mode==EpubRenderMode::Light?EpubRenderMode::Default:EpubRenderMode::Light;
    Section mismatch;assert(!mismatch.loadSectionFile(other));assert(mismatch.cleared);
    if(mode!=EpubRenderMode::Default) assert(readerRenderSpecSignature(spec)!=signature);
    Storage.saved[0]=73;Section old;assert(!old.loadSectionFile(spec));assert(old.cleared);
    Storage.saved[0]=SECTION_FILE_PARTIAL_VERSION;
    const uint32_t li=HEADER_SIZE, visible=HEADER_SIZE+4, consumed=200, total=400;
    memcpy(Storage.saved.data()+HEADER_SIZE-8,&li,4);memcpy(Storage.saved.data()+HEADER_SIZE-4,&visible,4);
    Storage.saved.resize(visible+2*4+8);
    memcpy(Storage.saved.data()+visible+2*4,&consumed,4);memcpy(Storage.saved.data()+visible+2*4+4,&total,4);
    Section partial;assert(partial.loadSectionFile(spec));assert(partial.partial_);
    assert(partial.partialPageCount_==2 && partial.partialBytesConsumed_==200 && partial.partialTotalBytes_==400);
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'lib/Epub'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Profile cache headers accept matching modes and reject mismatches and older versions')
