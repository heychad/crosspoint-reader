"""Exercise per-core exception metadata capture without touching a real stack."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'lib/hal/HalSystem.cpp').read_text()
def method(signature):
 start=s.index(signature);opening=s.index('{',start);depth=0
 for end in range(opening,len(s)):
  depth+=(s[end]=='{')-(s[end]=='}')
  if depth==0:return s[start:end+1]
 raise ValueError(signature)
state=s[s.index('RTC_NOINIT_ATTR char panicMessage'):s.index('extern "C"')]
program=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#define IRAM_ATTR
#define RTC_NOINIT_ATTR
#define DRAM_ATTR
#define __riscv 0
#define MAX_PANIC_STACK_DEPTH 32
#define PANIC_CAPTURE_MAGIC 0x50414E49u
#define CROSSPOINT_VERSION "test58-clipfix1"
namespace HalSystem{struct StackFrame{uint32_t sp,spp[8];};}
struct XtExcFrame{uint32_t pc,a0,a1,exccause,excvaddr;};
int delegated=0;
void __real_panic_abort(const char*){++delegated;}void __real_panic_print_backtrace(const void*,int){++delegated;}
bool esp_stack_ptr_is_sane(uint32_t){return false;}bool esp_ptr_in_dram(const void*){return false;}
int esp_reset_reason(){return 0;}const char*resetReasonName(int){return "PANIC";}
std::string getLastLogs(){return "saved log";}void clearLastLogs(){}
''' + state + '\nstatic const char PANIC_REASON_UNKNOWN[]="unknown";\n' + method('void IRAM_ATTR __wrap_panic_abort(') + '\n' + method('void IRAM_ATTR __wrap_panic_print_backtrace(') + '\n' + method('void clearPanic(') + '\n' + method('std::string getPanicInfo(') + r'''
int main(){
 clearPanic();XtExcFrame a{0x42001234,0x82000001,0,0x1c,0x17},b{0x42005678,0x82000002,0,0x1d,0x23};
 __wrap_panic_print_backtrace(&a,0);assert(panicRegistersMask==1&&panicCaptureMarker==PANIC_CAPTURE_MAGIC);
 __wrap_panic_print_backtrace(&b,1);assert(panicRegistersMask==3&&panicRegisters[0].pc==a.pc&&panicRegisters[1].pc==b.pc);
 auto report=getPanicInfo(true);assert(report.find("PC=0x42001234 cause=0x0000001C address=0x00000017")!=std::string::npos);
 assert(report.find("PC=0x42005678")!=std::string::npos);
 __wrap_panic_abort("abort message");assert(panicRegistersMask==0);
 __wrap_panic_print_backtrace(&b,1);assert(panicRegistersMask==2&&getPanicInfo(true).find("abort message")!=std::string::npos);
 __wrap_panic_print_backtrace(nullptr,0);assert(panicRegistersMask==2);
 clearPanic();assert(getPanicInfo(true).find("Core 1 registers")==std::string::npos);
 panicRegistersMask=0xffffffff;panicRegistersMagic=PANIC_REGISTERS_MAGIC;
 __wrap_panic_print_backtrace(&a,0);assert(panicRegistersMask==1);
 assert(delegated==6);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(program)
 subprocess.run(['c++','-std=c++20',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Both cores retain fault registers, corrupt-stack captures survive, and old epochs clear')
