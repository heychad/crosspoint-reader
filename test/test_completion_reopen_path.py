"""Exercise the actual reopen-path update and failure rollback."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/CrossPointState.cpp").read_text()
start = source.index("bool CrossPointState::updateOpenEpubPathPersisted(")
opening = source.index("{", start)
depth = 0
for index in range(opening, len(source)):
    if source[index] == "{":
        depth += 1
    elif source[index] == "}":
        depth -= 1
        if depth == 0:
            method = source[start:index + 1]
            break
else:
    raise RuntimeError("Method not found")

code = r'''
#include <cassert>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
class CrossPointState {
 public:
 std::string openEpubPath;
 std::mutex storeMutex;
 bool success=true; int writes=0;
 bool saveForCompletionLocked(){++writes;return success;}
 bool updateOpenEpubPathPersisted(const std::string&,const std::string&);
};
''' + method + r'''
int main(){
 CrossPointState state;
 state.openEpubPath="/old.epub";
 std::mutex storageMutex;
 std::unique_lock<std::mutex> storageLock(storageMutex);
 std::promise<void> acquired;
 std::thread other([&]{std::lock_guard<std::mutex> lock(state.storeMutex);acquired.set_value();
                       std::lock_guard<std::mutex> waitingForStorage(storageMutex);});
 acquired.get_future().wait();
 assert(!state.updateOpenEpubPathPersisted("/old.epub","/read/new.epub"));
 assert(state.openEpubPath=="/old.epub" && state.writes==0);
 storageLock.unlock();other.join();
 state.openEpubPath="/another.epub";
 assert(state.updateOpenEpubPathPersisted("/old.epub","/read/new.epub") && state.writes==0);
 state.openEpubPath="/old.epub";state.success=false;
 assert(!state.updateOpenEpubPathPersisted("/old.epub","/read/new.epub"));
 assert(state.openEpubPath=="/old.epub" && state.writes==1);
 state.success=true;
 assert(state.updateOpenEpubPathPersisted("/old.epub","/read/new.epub"));
 assert(state.openEpubPath=="/read/new.epub" && state.writes==2);
 assert(state.updateOpenEpubPathPersisted("/old.epub","/read/new.epub") && state.writes==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Completion reopen-path rollback and retry checks passed")
