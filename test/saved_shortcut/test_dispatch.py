"""Exercise shortcut dispatch and the production manager allocation branch."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/activities/reader/ReaderGestureInput.cpp").read_text()
start = source.index("    case Action::SavedItems:")
end = source.index("    case Action::", start + 10)
body = source[start:end]
manager = (root / "src/activities/ActivityManager.cpp").read_text()
manager_start = manager.index("void ActivityManager::goToSavedItems()")
manager_end = manager.index("\nvoid ActivityManager::", manager_start + 10)
manager_body = manager[manager_start:manager_end]
code = r'''
#include <cassert>
#include "personalization/ReaderShortcuts.h"
using readershortcuts::Action;
#include <memory>
#include <utility>
#define LOG_ERR(...) ((void)0)
bool canAllocate=true;
struct SavedItemsActivity { SavedItemsActivity(int&,int&){} };
template<class T,class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args){
 if(!canAllocate)return {};return std::make_unique<T>(std::forward<Args>(args)...);
}
struct ActivityManager {int renderer=0,mappedInput=0,calls=0;bool pending=false;
 void replaceActivity(std::unique_ptr<SavedItemsActivity> screen){++calls;pending=bool(screen);}
 void goToSavedItems();
};
''' + manager_body + r'''
ActivityManager activityManager;
struct Reader {
 int updates=0;void requestUpdate(){++updates;}
 bool dispatch(Action action){switch(action){
''' + body + r'''
 default:return false;}}
};
int main(){
 Reader reader;assert(reader.dispatch(Action::SavedItems));
 assert(activityManager.calls==1 && activityManager.pending && reader.updates==1);
 canAllocate=false;activityManager.pending=false;
 assert(reader.dispatch(Action::SavedItems));
 assert(activityManager.calls==1 && !activityManager.pending && reader.updates==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", "-I", str(root / "src"), str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Saved Items shortcut dispatch and allocation-failure repaint passed")
