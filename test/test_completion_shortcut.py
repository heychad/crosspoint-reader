"""Check the reader completion shortcut's dispatch and move guard."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubReaderShortcuts.cpp').read_text()
start = source.index('    case Action::ToggleCompletion: {')
end = source.index('    case Action::ToggleFocus:', start)
case = source[start:end]
code = r'''
#include <cassert>
#include <string>
#include "personalization/ReaderShortcuts.h"
#include "util/ReadFolder.h"
using readershortcuts::Action;
bool locked=false;
struct RenderLock { template<class T> RenderLock(T&) {assert(!locked);locked=true;} ~RenderLock(){locked=false;} };
struct Settings {bool moveFinishedToReadFolder=false;} SETTINGS;
struct Stats {bool known=true, finished=false;bool completionStatus(bool& value){value=finished;return known;}};
struct Reader {
 Stats readingStats;std::string bookPath="/Authors/book.epub";int calls=0;bool target=false;
 void handleCompletionAttempt(bool recovery,bool value){assert(!locked && !recovery);++calls;target=value;}
 bool dispatch(Action action){switch(action){
''' + case + r'''
 default:return false;}}
};
int main(){
 Reader reader;assert(reader.dispatch(Action::ToggleCompletion));
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) && defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT)
 assert(reader.calls==1 && reader.target);
 reader.readingStats.finished=true;reader.dispatch(Action::ToggleCompletion);assert(reader.calls==2 && !reader.target);
 reader.readingStats.known=false;reader.dispatch(Action::ToggleCompletion);assert(reader.calls==2);
 reader.readingStats.known=true;reader.readingStats.finished=false;SETTINGS.moveFinishedToReadFolder=true;
 reader.dispatch(Action::ToggleCompletion);assert(reader.calls==2);
 reader.bookPath="/READ/book.epub";reader.dispatch(Action::ToggleCompletion);assert(reader.calls==3 && reader.target);
#else
 assert(reader.calls==0);
#endif
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    for flags in ([], ['-DCROSSPOINT_EXPERIMENTAL_COMPLETION', '-DCROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT']):
        subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), *flags,
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('Completion shortcut dispatch and move guards passed')
