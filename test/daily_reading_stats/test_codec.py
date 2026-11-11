"""Check CrossInk-compatible daily counters without touching reader storage."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
program = r'''
#include <cassert>
#include "activities/reader/DailyReadingStatsCodec.h"
using namespace dailyreading;
int main() {
 Month sample;
 sample.generation=7;
 sample.days[0]={3600,1200};
 sample.days[30]={900,0};
 std::array<uint8_t,MONTH_BYTES> bytes;
 assert(encodeMonth(sample,9801,31,bytes));
 // Independent Python struct/FNV fixture for the pinned RC wire format.
 assert(bytes[0]==2 && bytes[1]=='D' && bytes[2]=='R' && !bytes[3]);
 assert(bytes[4]==0x49 && bytes[5]==0x26 && !bytes[6] && !bytes[7]);
 assert(bytes[12]==0x10 && bytes[13]==0x0e && bytes[16]==0xb0 && bytes[17]==4);
 assert(read32(bytes.data()+260)==0x4167669e);
 Month decoded;
 assert(decodeMonth(bytes.data(),bytes.size(),9801,31,decoded)==Decode::Valid && decoded==sample);
 assert(decodeMonth(bytes.data(),bytes.size(),9802,31,decoded)==Decode::Corrupt && decoded==Month{});
 for(size_t size=0;size<bytes.size();++size) {
  decoded=sample;
  assert(decodeMonth(bytes.data(),size,9801,31,decoded)!=Decode::Valid && decoded==Month{});
 }
 for(size_t i=0;i<bytes.size();++i) {
  auto bad=bytes;bad[i]^=0x80;
  assert(decodeMonth(bad.data(),bad.size(),9801,31,decoded)!=Decode::Valid);
 }
 assert(decodeMonth(bytes.data(),bytes.size(),9801,30,decoded)==Decode::Corrupt);
 auto bad=sample;bad.days[0].uploaded=3601;
 assert(!encodeMonth(bad,9801,31,bytes));
 assert(!encodeMonth(sample,9801,30,bytes));
 assert(!encodeMonth(sample,9801,27,bytes));
 assert(!encodeMonth(sample,9801,32,bytes));
 for(unsigned count=28;count<=31;++count) {
  Month month;month.days[count-1]={UINT32_MAX,UINT32_MAX};month.generation=UINT32_MAX;
  assert(encodeMonth(month,10000,count,bytes));
  assert(decodeMonth(bytes.data(),bytes.size(),10000,count,decoded)==Decode::Valid && decoded==month);
 }
 std::array<uint8_t,LEGACY_BYTES> old{};old[0]=1;
 write32(old.data()+1,7200);write32(old.data()+5,3600);
 write32(old.data()+9,checksum(old.data(),9,2166136261U ^ 9801));
 Counter counter;
 assert(decodeLegacy(old.data(),old.size(),9801,counter)==Decode::Valid);
 assert(counter==(Counter{7200,3600}));
 assert(decodeLegacy(old.data(),old.size(),9802,counter)==Decode::Corrupt && counter==Counter{});
 for(size_t size=0;size<old.size();++size)
  assert(decodeLegacy(old.data(),size,9801,counter)!=Decode::Valid);
 old[0]=2;assert(decodeLegacy(old.data(),old.size(),9801,counter)==Decode::Unsupported);
 Month first,second;
 assert(select(Decode::Missing,first,Decode::Missing,second)==Selection::Empty);
 assert(select(Decode::Corrupt,first,Decode::Missing,second)==Selection::Unavailable);
 assert(select(Decode::Valid,first,Decode::Corrupt,second)==Selection::First);
 assert(select(Decode::Corrupt,first,Decode::Valid,second)==Selection::Second);
 for(auto state:{Decode::Unsupported,Decode::Unreadable}) {
  assert(select(state,first,Decode::Valid,second)==Selection::Unavailable);
  assert(select(Decode::Valid,first,state,second)==Selection::Unavailable);
 }
 assert(select(Decode::Valid,first,Decode::Valid,second)==Selection::First);
 second.days[0].seconds=1;
 assert(select(Decode::Valid,first,Decode::Valid,second)==Selection::Unavailable);
 second={};second.generation=1;
 assert(select(Decode::Valid,first,Decode::Valid,second)==Selection::Second);
 first.generation=UINT32_MAX;second.generation=0;
 assert(select(Decode::Valid,first,Decode::Valid,second)==Selection::Second);
 first.generation=1;second.generation=0;
 assert(select(Decode::Valid,first,Decode::Valid,second)==Selection::First);
 first.generation=0;second.generation=0x80000000U;
 assert(select(Decode::Valid,first,Decode::Valid,second)==Selection::Unavailable);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.cpp').write_text(program)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I',str(root/'src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Daily counters: RC format, corruption, bounds, and slot selection passed')
