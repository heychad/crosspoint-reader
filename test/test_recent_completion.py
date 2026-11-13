"""Exercise the actual recent-book removal and failed-save retry behavior."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/RecentBooksStore.cpp').read_text()
def extract(name):
    start = source.index('bool RecentBooksStore::' + name + '(')
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise RuntimeError('Method not found')
method = extract('removeByPathPersisted') + '\n' + extract('updatePathPersisted')
code = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#define LOG_ERR(...) ((void)0)
struct RecentBook {std::string path, title, author, coverBmpPath;};
class RecentBooksStore {
public:
 std::vector<RecentBook> recentBooks;
 std::mutex storeMutex;
 bool success=true; int saves=0, finishes=0;
 bool saveToFile(){++saves;return success;}
 bool saveForCompletionLocked(bool finish=false){if(finish){++finishes;return success;}return saveToFile();}
 bool removeByPathPersisted(const std::string&);
 bool updatePathPersisted(const std::string&,const std::string&,const std::string&,const std::string&);
};
''' + method + r'''
int main(){
 RecentBooksStore store;
 store.recentBooks={{"first","1"},{"middle","2"},{"last","3"}};
 std::mutex storageMutex;
 std::unique_lock<std::mutex> storageLock(storageMutex);
 std::promise<void> acquired;
 std::thread other([&]{std::lock_guard<std::mutex> lock(store.storeMutex);acquired.set_value();
                       std::lock_guard<std::mutex> waitingForStorage(storageMutex);});
 acquired.get_future().wait();
 assert(!store.removeByPathPersisted("middle") && store.saves==0);
 assert(!store.updatePathPersisted("middle","new","","") && store.saves==0);
 assert(store.recentBooks.size()==3 && store.recentBooks[1].path=="middle");
 storageLock.unlock();other.join();
 assert(store.removeByPathPersisted("missing") && store.saves==0 && store.finishes==1);
 store.success=false;
 assert(!store.removeByPathPersisted("middle") && store.saves==1);
 assert(store.recentBooks.size()==3 && store.recentBooks[0].path=="first");
 assert(store.recentBooks[1].path=="middle" && store.recentBooks[1].title=="2");
 assert(store.recentBooks[2].path=="last");
 store.success=true;
 assert(store.removeByPathPersisted("middle") && store.saves==2);
 assert(store.recentBooks.size()==2 && store.recentBooks[0].path=="first" && store.recentBooks[1].path=="last");
 assert(store.removeByPathPersisted("middle") && store.saves==2);
 store.recentBooks[0].coverBmpPath="/old-cache/cover.bmp";
 store.success=false;
 assert(!store.updatePathPersisted("first","/read/book.epub","/old-cache","/new-cache"));
 assert(store.recentBooks[0].path=="first" && store.recentBooks[0].coverBmpPath=="/old-cache/cover.bmp");
 store.success=true;
 assert(!store.updatePathPersisted("first","last","/old-cache","/new-cache"));
 assert(store.updatePathPersisted("first","/read/book.epub","/old-cache","/new-cache"));
 assert(store.recentBooks[0].path=="/read/book.epub" && store.recentBooks[0].coverBmpPath=="/new-cache/cover.bmp");
 assert(store.updatePathPersisted("first","/read/book.epub","/old-cache","/new-cache"));
 store.success=false;assert(!store.updatePathPersisted("first","/read/book.epub","/old-cache","/new-cache"));
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
print('Recent-book persistence and retry checks passed')
