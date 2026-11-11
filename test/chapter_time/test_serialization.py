"""Verify chapter-time layouts remain readable by an older bar parser."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
arduinojson = Path(sys.argv[1]) if len(sys.argv) > 1 else root/'.pio/libdeps/x4pro/ArduinoJson/src'
code = r'''
#include <cassert>
#include <cstdio>
#include <string>
#include "personalization/ReaderBarsJson.h"
#include "personalization/CrossInkBarImport.h"
int main(int argc,char** argv){
 JsonDocument source,target;
 assert(!deserializeJson(source,R"({"readerStatusBars":{"version":1,"top":{"slots":[1,0,4,8,0,0,0],"percentageFormat":1,"progressBar":0,"thickness":2},"bottom":{"slots":[0,0,0,0,0,0,0],"percentageFormat":0,"progressBar":0,"thickness":0}}})"));
 target["unrelated"]=42;
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
 assert(crossinkimport::stageBars(source.as<JsonVariantConst>(),target)==crossinkimport::BarResult::Imported);
 ReaderBar imported;assert(readReaderBar(target["readerBarsV1"]["top"],imported));
 assert(imported.slots[2]==ReaderBarItem::TimeLeftChapter && target["unrelated"].as<int>()==42);
 assert(crossinkimport::stageBars(source.as<JsonVariantConst>(),target)==crossinkimport::BarResult::Unchanged);
 source["readerStatusBars"]["bottom"]["slots"][0]=6;
 assert(crossinkimport::stageBars(source.as<JsonVariantConst>(),target)==crossinkimport::BarResult::Unsupported);
 assert(readReaderBar(target["readerBarsV1"]["top"],imported) && imported.slots[2]==ReaderBarItem::TimeLeftChapter);
 auto bottom=source["readerStatusBars"]["bottom"];
 bottom["slots"][0]=0;
 bottom["slots"].clear();
 for (int value : {1,0,4,8,0,0}) bottom["slots"].add(value);
 assert(crossinkimport::stageBars(source.as<JsonVariantConst>(),target)==crossinkimport::BarResult::Imported);
 assert(readReaderBar(target["readerBarsV1"]["bottom"],imported));
 assert(imported.slots[2]==ReaderBarItem::Empty && imported.slots[3]==ReaderBarItem::TimeLeftChapter);
 assert(imported.slots[4]==ReaderBarItem::BookTitle);
#else
 assert(crossinkimport::stageBars(source.as<JsonVariantConst>(),target)==crossinkimport::BarResult::Unsupported);
 assert(target["readerBarsV1"].isUnbound() && target["unrelated"].as<int>()==42);
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
 if(argc==2){JsonDocument old;assert(!deserializeJson(old,argv[1]));ReaderBar fallback;
  assert(readReaderBar(old.as<JsonVariantConst>(),fallback));
  assert(fallback.slots[3]==ReaderBarItem::TimeLeftSection && fallback.slots[0]==ReaderBarItem::Clock);return 0;}
 ReaderBar bar;bar.slots[0]=ReaderBarItem::Clock;bar.slots[3]=ReaderBarItem::TimeLeftChapter;
 bar.slots[6]=ReaderBarItem::BookTitle;bar.progress=1;bar.thickness=2;
 JsonDocument doc;writeReaderBar(doc.to<JsonObject>(),bar);
 assert(doc["slots"][3].as<unsigned>()==static_cast<unsigned>(ReaderBarItem::TimeLeftSection));
 assert(doc["chapterTimeSlots"].as<unsigned>()==8);
 ReaderBar loaded;assert(readReaderBar(doc.as<JsonVariantConst>(),loaded) && loaded.slots==bar.slots);
 std::string out;serializeJson(doc,out);std::puts(out.c_str());
 doc["chapterTimeSlots"]=128;assert(readReaderBar(doc.as<JsonVariantConst>(),loaded));
 assert(loaded.slots[3]==ReaderBarItem::TimeLeftSection && loaded.slots[0]==ReaderBarItem::Clock);
 doc["chapterTimeSlots"]=1;assert(readReaderBar(doc.as<JsonVariantConst>(),loaded));
 assert(loaded.slots[0]==ReaderBarItem::Clock && loaded.slots[3]==ReaderBarItem::TimeLeftSection);
#else
 assert(argc==2);JsonDocument doc;assert(!deserializeJson(doc,argv[1]));ReaderBar loaded;
 assert(readReaderBar(doc.as<JsonVariantConst>(),loaded));
 assert(loaded.slots[0]==ReaderBarItem::Clock && loaded.slots[3]==ReaderBarItem::TimeLeftSection);
 assert(loaded.slots[6]==ReaderBarItem::BookTitle && loaded.progress==1 && loaded.thickness==2);
 JsonDocument rewritten;writeReaderBar(rewritten.to<JsonObject>(),loaded);
 std::string out;serializeJson(rewritten,out);std::puts(out.c_str());
#endif
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.cpp').write_text(code)
    for name,flags in [('new',['-DCROSSPOINT_EXPERIMENTAL_CHAPTER_TIME=1']),('old',[])]:
        subprocess.run(['c++','-std=c++20',*flags,'-I',str(root/'src'),
                        '-I',str(arduinojson),str(path/'test.cpp'),'-o',str(path/name)],check=True)
    data=subprocess.check_output([str(path/'new')],text=True).strip()
    rewritten=subprocess.check_output([str(path/'old'),data],text=True).strip()
    subprocess.run([str(path/'new'),rewritten],check=True)
print('Chapter-time layouts retain their other slots in an older reader-bar parser')
