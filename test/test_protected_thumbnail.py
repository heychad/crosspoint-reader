"""Exercise source-thumbnail ordering with a protected guide cover."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'lib/Epub/Epub.cpp').read_text()
start = source.index('bool Epub::generateThumbBmpFromSource(')
end = source.index('bool Epub::generateThumbBmpForCover(', start)
body = source[start:end]
code = r'''
#include <memory>
#include <string>
#include <utility>
#define LOG_ERR(...) do {} while (0)
struct MetadataCache { struct BookMetadata { std::string coverItemHref; }; };
using BookMetadataCache = MetadataCache;
struct StorageModel { bool exists(const char*) const { return false; } } Storage;
namespace Txt {
bool isTxtOrMd(const std::string& path) { return path == "text.txt"; }
std::string findCompanionCoverImage(const std::string&) { return "cover.jpg"; }
}
template<class T, class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct ZipFile { explicit ZipFile(const std::string&) {} bool open() { return true; } };
struct Epub {
  std::string filepath = "book.epub";
  bool needsProtection = true, allowProtection = true, opened = false;
  int protectionCalls = 0, parses = 0, generated = 0;
  std::unique_ptr<int> decryptor;
  std::string getThumbBmpPath(int) const { return "thumb.bmp"; }
  bool openProtection() {
    ++protectionCalls;
    if (!allowProtection) return false;
    opened = true;
    if (needsProtection) decryptor = std::make_unique<int>(1);
    return true;
  }
  bool parseContentOpf(MetadataCache::BookMetadata& metadata, bool, bool, ZipFile*) {
    ++parses;
    if (needsProtection && !decryptor) return false;
    metadata.coverItemHref = "guide-cover.jpg";
    return true;
  }
  void setupCacheDir() {}
  bool generateThumbBmpForCover(int, const std::string& path) {
    ++generated;
    return !path.empty();
  }
  bool generateThumbBmpFromSource(int);
};
''' + body + r'''
int main() {
  Epub protectedBook;
  if (!protectedBook.generateThumbBmpFromSource(100) || protectedBook.protectionCalls != 1 ||
      protectedBook.parses != 1 || protectedBook.generated != 1) return 1;
  Epub denied; denied.allowProtection = false;
  if (denied.generateThumbBmpFromSource(100) || denied.parses || denied.generated) return 2;
  Epub plain; plain.needsProtection = false;
  if (!plain.generateThumbBmpFromSource(100) || plain.protectionCalls != 1 || plain.generated != 1) return 3;
  Epub text; text.filepath = "text.txt";
  if (!text.generateThumbBmpFromSource(100) || text.protectionCalls || text.generated != 1) return 4;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Protected cover thumbnail checks passed')
