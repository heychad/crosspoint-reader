"""Test bounded automatic-backup retention selection; this does not delete files."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
code = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include "AutomaticBackupRetention.h"
using statsbackup::AutomaticBackupRetention;
int main(){
 constexpr char current[]="stats_cp_20261110_A_0000000000000001.bin";
 AutomaticBackupRetention plan(current);
 plan.observe("stats_2020-01-01.bin");
 plan.observe("stats_cp_00000000_U_0000000000000001.bin");
 plan.observe("stats_cp_0000000001_M_00000000_000000_0000000000000001.bin");
 plan.observe("stats_cp_20261110_A_000000000000000G.bin");
 assert(plan.count()==0 && !plan.candidate(true));
 for(int day=1;day<=10;++day){
  char name[41];std::snprintf(name,sizeof(name),"stats_cp_202611%02d_A_0000000000000001.bin",day);
  plan.observe(name);
 }
 assert(plan.count()==10 && !plan.candidate(false));
 assert(plan.candidate(true) && std::strcmp(plan.candidate(true),"stats_cp_20261101_A_0000000000000001.bin")==0);
 AutomaticBackupRetention missing(current);
 for(int day=1;day<=9;++day){char name[41];std::snprintf(name,sizeof(name),"stats_cp_202611%02d_A_0000000000000001.bin",day);missing.observe(name);}
 assert(!missing.candidate(true));
 AutomaticBackupRetention protectedOldest("stats_cp_20261101_A_0000000000000001.bin");
 for(int day=1;day<=8;++day){char name[41];std::snprintf(name,sizeof(name),"stats_cp_202611%02d_A_0000000000000001.bin",day);protectedOldest.observe(name);}
 assert(std::strcmp(protectedOldest.candidate(true),"stats_cp_20261102_A_0000000000000001.bin")==0);
 AutomaticBackupRetention underLimit(current);
 underLimit.observe(current);
 assert(!underLimit.candidate(true));
 for(char group : {'M','U'}) {
  char latest[59];std::snprintf(latest,sizeof(latest),"stats_cp_0000000010_%c_00000000_000000_0000000000000001.bin",group);
  AutomaticBackupRetention grouped(latest);
  grouped.observe(current);
  grouped.observe("stats_cp_00000000_U_0000000000000001.bin");
  for(int n=1;n<=10;++n){char name[59];std::snprintf(name,sizeof(name),"stats_cp_%010d_%c_00000000_000000_0000000000000001.bin",n,group);grouped.observe(name);}
  assert(grouped.count()==10 && grouped.candidate(true));
  uint32_t next=0;assert(AutomaticBackupRetention::sequence(grouped.candidate(true),next) && next==1);
 }
 uint32_t untouched=99;
 assert(!AutomaticBackupRetention::sequence("stats_cp_9999999999_U_00000000_000000_0000000000000001.bin",untouched));
 assert(untouched==99);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I',
                    str(root / 'src/activities/reader'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Grouped retention protects the current snapshot and requires a complete scan')
