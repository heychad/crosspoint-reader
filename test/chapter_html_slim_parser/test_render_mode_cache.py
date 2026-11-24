"""Exercise the production section header writer and loader with each profile."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()


def method(signature):
    start = source.index(signature)
    position = source.index('{', start)
    depth = 0
    for i in range(position, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if not depth:
            return source[start:i + 1]
    raise AssertionError(signature)


header = re.search(r'constexpr uint32_t HEADER_SIZE\s*=\s*.*?;', source, re.S).group()
version = int(re.search(r'constexpr uint8_t SECTION_FILE_VERSION = (\d+);', source).group(1))
assert 28 < version < 0xFE
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "Epub/ReaderRenderSpec.h"
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
constexpr uint8_t SECTION_FILE_VERSION = CACHE_VERSION_TOKEN;
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = 0xFE - (SECTION_FILE_VERSION - 28);
struct File {
  std::vector<uint8_t> bytes; size_t pos = 0, shortReadAt = SIZE_MAX; bool failSeek = false;
  explicit operator bool() const { return true; }
  size_t write(const uint8_t* in, size_t n) {bytes.insert(bytes.end(), in, in + n); return n;}
  size_t read(uint8_t* out, size_t n) {if(pos>bytes.size())return 0; n=std::min(n,bytes.size()-pos); if(pos==shortReadAt && n)--n; memcpy(out,bytes.data()+pos,n); pos+=n; return n;}
  bool seek64(uint64_t next) {if(failSeek || next>bytes.size())return false; pos=next;return true;}
  uint64_t fileSize64() const {return bytes.size();}
  size_t size() const {return bytes.size();}
  bool close() {return true;}
};
namespace serialization {
template<class T> bool tryWritePod(File& f, const T& value) {return f.write(reinterpret_cast<const uint8_t*>(&value), sizeof value)==sizeof value;}
template<class T> bool tryReadPod(File& f, T& value) {return f.read(reinterpret_cast<uint8_t*>(&value), sizeof value)==sizeof value;}
template<class T> void readPod(File& f, T& value) {f.read(reinterpret_cast<uint8_t*>(&value), sizeof value);}
}
struct StorageType {
  std::vector<uint8_t> saved;
  bool openFileForRead(const char*, const std::string&, File& f) {f.bytes=saved;f.pos=0;return true;}
} Storage;
bool recoverSectionCache(const std::string&) {return true;}
void retireSectionCacheBackup(const std::string&) {}
struct Section {
  File file; std::string filePath; uint16_t pageCount=2, partialPageCount_=0;
  uint32_t partialBytesConsumed_=0, partialTotalBytes_=0; bool partial_=false, cleared=false;
  bool writeSectionFileHeader(const ReaderRenderSpec&);
  bool loadSectionFile(const ReaderRenderSpec&);
  bool clearCache() {cleared=true; return true;}
};
''' + header + '\n' + method('struct SectionPageTables') + ';\n' + method('bool Section::writeSectionFileHeader(') + '\n' + method('bool Section::loadSectionFile(') + r'''
int main() {
  ReaderRenderSpec original; original.viewportWidth=480;original.viewportHeight=800;
  auto signature=readerRenderSpecSignature(original);
  for(auto indent : {ParagraphIndentMode::FixedSpaces,ParagraphIndentMode::Book,ParagraphIndentMode::Force}) {
  for (auto mode : {EpubRenderMode::Default, EpubRenderMode::Balanced, EpubRenderMode::Light}) {
    ReaderRenderSpec spec=original;spec.renderMode=mode;spec.paragraphIndentMode=indent;Section written;assert(written.writeSectionFileHeader(spec));
    assert(written.file.bytes.size()==HEADER_SIZE);Storage.saved=written.file.bytes;
    Storage.saved[0]=SECTION_FILE_VERSION;
    Section match;assert(match.loadSectionFile(spec));assert(match.pageCount==2);assert(!match.cleared);
    ReaderRenderSpec other=spec;other.renderMode=mode==EpubRenderMode::Light?EpubRenderMode::Default:EpubRenderMode::Light;
    Section mismatch;assert(!mismatch.loadSectionFile(other));assert(!mismatch.cleared);
    ReaderRenderSpec otherIndent=spec;otherIndent.paragraphIndentMode=indent==ParagraphIndentMode::Book?ParagraphIndentMode::Force:ParagraphIndentMode::Book;
    Section differentIndent;assert(!differentIndent.loadSectionFile(otherIndent));assert(!differentIndent.cleared);
    if(mode!=EpubRenderMode::Default) assert(readerRenderSpecSignature(spec)!=signature);
    Storage.saved[0]=SECTION_FILE_VERSION-1;Section old;assert(!old.loadSectionFile(spec));assert(old.cleared);
    Storage.saved[0]=0xFE - (SECTION_FILE_VERSION-1-28);Section oldPartial;
    assert(!oldPartial.loadSectionFile(spec));assert(oldPartial.cleared);
    Storage.saved[0]=SECTION_FILE_PARTIAL_VERSION;
    const uint32_t pages=HEADER_SIZE+4, anchors=pages+8, paragraphs=anchors+2;
    const uint32_t listItems=paragraphs+6, visible=listItems+4, consumed=200, total=400;
    const uint32_t tables[]={pages,anchors,paragraphs,listItems,visible};
    memcpy(Storage.saved.data()+HEADER_SIZE-sizeof(tables),tables,sizeof(tables));
    Storage.saved.resize(visible+2*4+8);
    memcpy(Storage.saved.data()+visible+2*4,&consumed,4);memcpy(Storage.saved.data()+visible+2*4+4,&total,4);
    Section partial;assert(partial.loadSectionFile(spec));assert(partial.partial_);
    assert(partial.partialPageCount_==2 && partial.partialBytesConsumed_==200 && partial.partialTotalBytes_==400);
    for (const size_t offset : {size_t(HEADER_SIZE-22),size_t(HEADER_SIZE-20),size_t(visible+8),size_t(visible+12)}) {
      Section fault;
      fault.file.shortReadAt=offset;
      assert(!fault.loadSectionFile(spec) && !fault.partial_ && !fault.cleared);
    }
    Section seekFault;seekFault.file.failSeek=true;
    assert(!seekFault.loadSectionFile(spec) && !seekFault.partial_ && !seekFault.cleared);
    const uint32_t wrapped=UINT32_MAX-3;
    memcpy(Storage.saved.data()+HEADER_SIZE-4,&wrapped,4);
    Section invalid;assert(!invalid.loadSectionFile(spec) && invalid.cleared && !invalid.partial_);
    Section otherPartial;assert(!otherPartial.loadSectionFile(otherIndent));
  }
  }
}
'''
code = code.replace('CACHE_VERSION_TOKEN', str(version))
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'lib/Epub'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Profile cache headers accept matching modes and reject mismatches and older versions')
