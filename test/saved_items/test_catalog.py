"""Run the production catalog scan and row provider with deterministic storage."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'src/activities/home/SavedItemsActivity.cpp').read_text()
def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define STR_SAVED_ITEMS_ERROR 0
const char* tr(int){return "Error";}
int utf8SafeTruncateBuffer(const char*,int length){return length;}
int lockDepth=0;
struct RenderLock {template<class T>RenderLock(T&){assert(!lockDepth++);}~RenderLock(){--lockDepth;}};
struct Book {std::string path,title,name,author;bool exists=true,readable=true;uint8_t kinds=1;};
std::vector<Book> books;
namespace library {
 constexpr unsigned CLIX_FLAG_SCAN_COMPLETE = 1;
 enum class SortOrder {TitleAsc};
 struct ClixRecord {uint16_t id;};
 struct LibraryIndexFile {
  struct PathLookup {bool ready=false;explicit operator bool()const{return ready;}};
  int failRecord=-1;int pathReads=0;
  struct { unsigned flags = CLIX_FLAG_SCAN_COMPLETE; } indexHeader;
  const auto& header() const { return indexHeader; }
  uint16_t bookCount()const{return books.size();}
  uint16_t ordinalForRow(SortOrder,uint16_t row){assert(lockDepth);return row;}
  bool readRecord(uint16_t ordinal,ClixRecord& out){assert(lockDepth);out.id=ordinal;return ordinal!=failRecord;}
  bool readPath(const ClixRecord& r,std::string& path,const PathLookup& lookup){assert(lockDepth&&lookup.ready);++pathReads;path=books[r.id].path;return true;}
  bool readTitle(const ClixRecord& r,std::string& text){text=books[r.id].title;return !text.empty();}
  bool readName(const ClixRecord& r,std::string& text){text=books[r.id].name;return true;}
  bool readAuthor(const ClixRecord& r,std::string& text){text=books[r.id].author;return true;}
 };
}
struct {int probes=0;bool exists(const char* path){++probes;for(const auto& b:books)if(b.path==path)return b.exists;return false;}} Storage;
namespace FsHelpers {bool hasReflowableBookExtension(const std::string& path){return !path.ends_with(".xtc");}}
namespace saveditems {
 constexpr uint8_t ALL_KINDS = 7;
 struct Candidates {
   std::set<std::string> possible;
   bool unchanged = true;
   bool advance() const { return true; }
   bool valid() const { return unchanged; }
   bool empty() const { return possible.empty(); }
   uint8_t kindsFor(const std::string& path) const { return possible.contains(path) ? ALL_KINDS : 0; }
 };
 bool inspect(const std::string& path,uint8_t& kinds,uint8_t candidates){
   assert(lockDepth && candidates);for(const auto& b:books)if(b.path==path){kinds=b.kinds&candidates;return b.readable;}
   assert(false);return false;
 }
}
namespace fui {struct ListItem{const char* label=nullptr;const char* subtitle=nullptr;int actionValue=-1;};}
struct SavedItemsActivity {
 enum class State {Scanning,Ready,Error};
 library::LibraryIndexFile index;
 library::ClixRecord record{};
 library::LibraryIndexFile::PathLookup scanPaths{true};
 std::unique_ptr<saveditems::Candidates> sidecars;
 std::unique_ptr<uint16_t[]> ordinals;
 uint16_t count=0,scanned=0;
 State state=State::Scanning;bool incomplete=false;
 std::string scratch;char title[513]{},author[129]{};int updates=0;
 explicit SavedItemsActivity(size_t total):ordinals(std::make_unique<uint16_t[]>(total)){scratch.reserve(512);}
 void requestUpdate(){++updates;}
 void scanBatch();
 bool discardChangedSidecars();
 static void provideRow(void*,uint16_t,fui::ListItem&);
};
'''
code += method('void SavedItemsActivity::scanBatch(')
code += method('bool SavedItemsActivity::discardChangedSidecars(')
code += method('void SavedItemsActivity::provideRow(')
code += r'''
int main(){
  for(int i=0;i<4096;++i) books.push_back({"/folder/"+std::to_string(i)+".epub","", "book"+std::to_string(i),"Author"});
  books[2].kinds=0;books[3].exists=false;books[4].path="/other.xtc";books[5].readable=false;books[5].kinds=0;
  SavedItemsActivity activity(books.size());
  while(activity.state==SavedItemsActivity::State::Scanning)activity.scanBatch();
  assert(activity.count==4093 && activity.incomplete && !activity.scanPaths);
  assert(activity.index.pathReads==4096 && lockDepth==0);
  fui::ListItem row;
  {
    RenderLock lock(activity);
    activity.provideRow(&activity,0,row);
    assert(std::string(row.label)=="book0" && std::string(row.subtitle)=="Author");
    activity.provideRow(&activity,activity.count-1,row);
    assert(std::string(row.label)=="book4095");
  }
  SavedItemsActivity damaged(books.size());damaged.index.failRecord=42;
  while(damaged.state==SavedItemsActivity::State::Scanning)damaged.scanBatch();
  assert(damaged.state==SavedItemsActivity::State::Error && damaged.count==39 && lockDepth==0);
  Storage.probes=0;
  SavedItemsActivity empty(books.size());
  empty.sidecars=std::make_unique<saveditems::Candidates>();
  empty.scanBatch();
  assert(empty.state==SavedItemsActivity::State::Ready && !empty.count && !empty.index.pathReads && !Storage.probes);

  SavedItemsActivity sparse(books.size());
  sparse.sidecars=std::make_unique<saveditems::Candidates>();
  sparse.sidecars->possible={books[0].path,books[5].path};
  while(sparse.state==SavedItemsActivity::State::Scanning)sparse.scanBatch();
  assert(sparse.count==2 && sparse.incomplete && Storage.probes==2 && sparse.updates==1);

  SavedItemsActivity changed(books.size());
  changed.sidecars=std::make_unique<saveditems::Candidates>();
  changed.sidecars->possible={books[0].path};
  changed.scanBatch();
  assert(changed.count==1);
  changed.sidecars->unchanged=false;
  changed.scanBatch();
  assert(!changed.sidecars && changed.count==0 && changed.scanned==0);
  while(changed.state==SavedItemsActivity::State::Scanning)changed.scanBatch();
  assert(changed.count==4093 && changed.incomplete && changed.updates==1);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory=Path(temporary)
    (directory/'catalog.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',*shlex.split(os.environ.get('CXXFLAGS','')),
                    str(directory/'catalog.cpp'),'-o',str(directory/'catalog')],check=True)
    subprocess.run([str(directory/'catalog')],check=True)
print('Production 4096-book scan, checked path lookup, missing data, index failure and filename fallback passed')
