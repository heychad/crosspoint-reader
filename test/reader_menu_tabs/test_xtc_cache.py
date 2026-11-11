"""Exercise production XTC image deletion with the storage fault model."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "lib/Xtc/Xtc.cpp").read_text()
start = source.index("bool Xtc::clearImageCache() const")
end = source.index("void Xtc::setupCacheDir() const", start)
code = r'''
#include <HalStorage.h>
#include <cassert>
#include <cstring>
#include <strings.h>
#include <string>
struct Xtc {std::string cachePath;bool clearImageCache()const;};
''' + source[start:end] + r'''
const std::string cache="/.crosspoint/xtc_123";
void write(const std::string& name){HalFile file;assert(Storage.openFileForWrite("TEST",cache+"/"+name,file));
 assert(file.write(uint8_t{42})==1);assert(file.close());}
bool exists(const std::string& name){return Storage.exists((cache+"/"+name).c_str());}
int main(){
 Storage.reset();Xtc book{cache};assert(book.clearImageCache());
 for(const auto* name:{"cover.bmp","thumb_1.bmp","thumb_800.bmp","thumb_2147483647.bmp","THUMB_400.BMP",
                      "progress.bin","progress.bin.tmp","stats_v5.bin","stats_v5.bin.bak",
                      "reader_settings.bin","reading_stats_off","bookmarks.bin","unknown.bin",
                      "thumb_.bmp","thumb_bad.bmp","thumb_1.bmp.bak","cover.bmp.bak"})write(name);
 assert(Storage.mkdir((cache+"/thumb_2.bmp").c_str()));
 write("nested/cover.bmp");
 assert(book.clearImageCache());
 for(const auto* name:{"cover.bmp","thumb_1.bmp","thumb_800.bmp","thumb_2147483647.bmp","THUMB_400.BMP"})assert(!exists(name));
 for(const auto* name:{"progress.bin","progress.bin.tmp","stats_v5.bin","stats_v5.bin.bak",
                      "reader_settings.bin","reading_stats_off","bookmarks.bin","unknown.bin",
                      "thumb_.bmp","thumb_bad.bmp","thumb_1.bmp.bak","cover.bmp.bak","nested/cover.bmp"}){
  HalFile file;assert(Storage.openFileForRead("TEST",cache+"/"+name,file));
  uint8_t value=0;assert(file.read(&value,1)==1&&value==42);assert(file.close());
 }
 assert(exists("thumb_2.bmp"));assert(book.clearImageCache());
 write("cover.bmp");Storage.failNextRemove(cache+"/cover.bmp");
 assert(!book.clearImageCache()&&exists("cover.bmp")&&exists("progress.bin"));
 assert(book.clearImageCache()&&!exists("cover.bmp"));
 write("cover.bmp");Storage.failNextCloseFor(cache+"/cover.bmp");
 assert(!book.clearImageCache()&&exists("cover.bmp"));
 assert(book.clearImageCache()&&!exists("cover.bmp"));
 write("cover.bmp");Storage.failNextStorageLock();
 assert(!book.clearImageCache()&&exists("cover.bmp"));
 Storage.failReadsFor(cache);assert(!book.clearImageCache()&&exists("cover.bmp"));
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", "-I", str(root / "test/book_reading_stats_atomic/stubs"),
                    str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("XTC image cache reset preserves state and handles storage failures")
