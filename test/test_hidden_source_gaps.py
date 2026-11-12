"""Run the production gap recorder with checked-allocation failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'lib/Epub/Epub/ParsedText.cpp').read_text()
header = (root / 'lib/Epub/Epub/ParsedText.h').read_text()


def body(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[start:end + 1]
    raise ValueError(signature)


program = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#define LOG_ERR(...) ((void)0)
bool failNext=false;unsigned allocations=0;
template<class T>std::unique_ptr<T>makeUniqueNoThrow(){
  ++allocations;if(std::exchange(failNext,false))return{};return std::make_unique<T>();
}
struct ParsedText {
''' + body(header, 'struct HiddenSourceChunk') + r''';
  std::unique_ptr<HiddenSourceChunk>hiddenSourceHead;
  HiddenSourceChunk*hiddenSourceTail=nullptr;bool droppedWords=false;
  bool recordHiddenSourceGap(uint32_t,uint32_t);
  uint32_t sourceOffsetBeforeNormalization(uint32_t,uint32_t)const;
};
''' + body(source, 'bool ParsedText::recordHiddenSourceGap(') + '\n' + body(
    source, 'uint32_t ParsedText::sourceOffsetBeforeNormalization(') + r'''
int main(){
  ParsedText text;
  for(unsigned i=0;i<8;++i)assert(text.recordHiddenSourceGap(3*i+1,1));
  assert(allocations==1&&text.hiddenSourceTail->count==8);
  auto*first=text.hiddenSourceHead.get();failNext=true;
  assert(!text.recordHiddenSourceGap(25,1)&&text.droppedWords);
  assert(text.hiddenSourceHead.get()==first&&!first->next&&first->count==8);
  assert(text.sourceOffsetBeforeNormalization(0,16)==24);
  assert(text.recordHiddenSourceGap(25,1));
  assert(first->next&&text.hiddenSourceTail->count==1);
  ParsedText large;
  assert(large.recordHiddenSourceGap(2,70000));
  assert(large.recordHiddenSourceGap(70002,100));
  assert(large.hiddenSourceTail->count==1&&large.hiddenSourceTail->gaps[0].length==70100);
  assert(large.sourceOffsetBeforeNormalization(0,9)==70109);
  assert(!large.recordHiddenSourceGap(3,1)&&large.droppedWords);
  ParsedText overflow;
  assert(!overflow.recordHiddenSourceGap(UINT32_MAX,1)&&overflow.droppedWords);
  ParsedText empty;const auto before=allocations;
  assert(empty.recordHiddenSourceGap(0,0)&&allocations==before&&!empty.hiddenSourceHead);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address,undefined',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Hidden source gap allocation failure, preservation, merging, and bounds passed')
