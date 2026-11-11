"""Exercise page-write failure through the production incremental build loop."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()


def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == '{') - (source[index] == '}')
        if not depth:
            return source[start:index + 1]
    raise ValueError(signature)


code = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include "EpubBuildFailure.h"
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
struct File { bool open=true; uint32_t position(){return 32;} explicit operator bool() const{return open;} };
struct Page { bool ok; bool serialize(File&){return ok;} };
struct ChapterHtmlSlimParser { enum class ParseStatus { Error, Done, More }; };
struct Section;
struct Parser {
  Section* section;
  bool failMemory=false;
  ChapterHtmlSlimParser::ParseStatus parseStep();
  uint32_t parseBytesConsumed(){return 1;}
  EpubBuildFailure lastParseFailure(){return EpubBuildFailure::OutOfMemory;}
};
struct Build { std::unique_ptr<Parser> parser; uint32_t bytesConsumed=0; };
struct Section {
  File file; bool buildPageWriteFailed_=false, abandoned=false, finalized=false;
  EpubBuildFailure buildFailure_=EpubBuildFailure::None;
  unsigned builtPageCount_=0, pageCount=0;
  std::unique_ptr<Build> build_=std::make_unique<Build>();
  uint32_t onPageComplete(std::unique_ptr<Page>);
  bool buildSomeMore(int,bool(*)(),bool(*)(void*),void*);
  void abandonBuild(){abandoned=true;build_.reset();}
  bool finalizeBuild(){finalized=true;return true;}
};
ChapterHtmlSlimParser::ParseStatus Parser::parseStep() {
  if (failMemory) return ChapterHtmlSlimParser::ParseStatus::Error;
  section->onPageComplete(std::make_unique<Page>(Page{false}));
  return ChapterHtmlSlimParser::ParseStatus::Done;
}
''' + method('uint32_t Section::onPageComplete(') + '\n' + method('bool Section::buildSomeMore(') + r'''
int main() {
  Section section;
  section.build_->parser=std::make_unique<Parser>(); section.build_->parser->section=&section;
  assert(!section.buildSomeMore(1,nullptr,nullptr,nullptr));
  assert(section.buildPageWriteFailed_ && section.abandoned && !section.finalized);
  assert(section.buildFailure_==EpubBuildFailure::InputOutput);
  assert(section.pageCount==0 && section.builtPageCount_==0);
  assert(section.onPageComplete(std::make_unique<Page>(Page{true}))==0);
  Section missing; missing.file.open=false;
  assert(missing.onPageComplete(std::make_unique<Page>(Page{true}))==0 && missing.buildPageWriteFailed_);
  Section memory; memory.build_->parser=std::make_unique<Parser>();
  memory.build_->parser->section=&memory; memory.build_->parser->failMemory=true;
  assert(!memory.buildSomeMore(1,nullptr,nullptr,nullptr));
  assert(memory.abandoned && memory.buildFailure_==EpubBuildFailure::OutOfMemory);
}
'''
assert 'if (!parsed || buildPageWriteFailed_)' in source
assert 'if (buildPageWriteFailed_) {\n    abandonBuild();\n    return;' in source
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'lib/Epub/Epub'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Failed page writes abandon the replacement without finalizing')
