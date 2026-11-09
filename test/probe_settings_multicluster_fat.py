"""Prototype a bounded FAT settings-chain reader in an isolated SdFat copy."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
dep = root / '.pio/libdeps/x4pro/SdFat'
spec = importlib.util.spec_from_file_location('patcher', root / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
fixture = (root / 'test/stats_safe_replace_real/RealStatsReplaceTest.cpp').read_text()
prefix = fixture[:fixture.index('#ifndef STATS_PAYLOAD_SIZE')]
prefix = prefix.replace('#include <HalStorage.h>', '').replace('#include <StatsSafeReplace.h>', '')
prefix += '\n#include <FsLib/FsFile.h>\n#include <SmallFileIdentity.h>\n'
method = r'''
bool FatFile::settingsClusterList(Cluster_t* out, size_t capacity, size_t* count) {
  if (!out || !count || !isFile() || !fileSize() || fileSize() > 32768 || m_firstCluster < 2) return false;
  const uint8_t shift = m_vol->bytesPerClusterShift();
  if (shift >= 63) return false;
  const uint64_t clusterBytes = uint64_t{1} << shift;
  const size_t expected = (uint64_t(fileSize()) + clusterBytes - 1) / clusterBytes;
  if (!expected || expected > capacity || expected > 64) return false;
  Cluster_t current = m_firstCluster;
  for (size_t i = 0; i < expected; ++i) {
    if (current < 2 || current - 2 >= m_vol->clusterCount()) return false;
    for (size_t j = 0; j < i; ++j) if (out[j] == current) return false;
    out[i] = current;
    Cluster_t next = 0;
    bool strictEoc = false;
    const int result = m_vol->fatGet(current, &next, &strictEoc);
    if (i + 1 == expected) { if (result != 0 || !strictEoc) return false; }
    else { if (result != 1) return false; current = next; }
  }
  *count = expected;
  return true;
}
'''
exfat_method = r'''
bool ExFatFile::settingsClusterList(Cluster_t* out, size_t capacity, size_t* count) {
  if (!out || !count || !isFile() || !m_dataLength || m_dataLength > 32768 ||
      m_validLength != m_dataLength || m_firstCluster < 2) return false;
  const uint8_t shift = m_vol->bytesPerClusterShift();
  if (shift >= 63) return false;
  const uint64_t clusterBytes = uint64_t{1} << shift;
  const size_t expected = (m_dataLength + clusterBytes - 1) / clusterBytes;
  if (!expected || expected > capacity || expected > 64) return false;
  Cluster_t current = m_firstCluster;
  for (size_t i = 0; i < expected; ++i) {
    if (current < 2 || current - 2 >= m_vol->clusterCount()) return false;
    for (size_t j = 0; j < i; ++j) if (out[j] == current) return false;
    const uint32_t bit = current - 2;
    const uint8_t sectorShift = m_vol->bytesPerSectorShift();
    const Sector_t sector = m_vol->clusterHeapStartSector() + (bit >> (sectorShift + 3));
    const uint8_t* data = m_vol->bitmapCachePrepare(sector, FsCache::CACHE_FOR_READ);
    if (!data || !(data[(bit >> 3) & ((uint32_t{1} << sectorShift) - 1)] & (uint8_t{1} << (bit & 7)))) return false;
    out[i] = current;
    if (isContiguous()) {
      if (i + 1 < expected) { if (current == UINT32_MAX) return false; ++current; }
    } else {
      Cluster_t next = 0;
      const int result = m_vol->fatGet(current, &next);
      if (i + 1 == expected) { if (result != 0) return false; }
      else { if (result != 1) return false; current = next; }
    }
  }
  *count = expected;
  return true;
}
'''
body = r'''
int main() {
  Disk disk; disk.sectorsTotal = 131072; uint8_t scratch[512]; FatFormatter formatter; assert(formatter.format(&disk,scratch));
  FsVolume v; assert(v.begin(&disk)); const size_t clusterBytes=v.bytesPerCluster();
  assert(v.fatType()==16 && clusterBytes<16384);
  const std::string data(clusterBytes*2+13,'q');
  auto f=v.open("/settings.json",O_CREAT|O_EXCL|O_RDWR);assert(f && f.write(data.data(),data.size())==data.size()&&f.sync());
  Cluster_t list[64]{};size_t n=999;assert(f.settingsClusterList(list,64,&n)&&n==3);
  assert(list[0]!=list[1]&&list[1]!=list[2]&&list[0]!=list[2]);
  size_t tooSmall=999;assert(!f.settingsClusterList(list,2,&tooSmall)&&tooSmall==999);
  assert(f.close());
  auto second=v.open("/staged.json",O_CREAT|O_EXCL|O_RDWR);
  assert(second&&second.write(data.data(),data.size())==data.size()&&second.sync());
  Cluster_t other[64]{};size_t otherCount=0;assert(second.settingsClusterList(other,64,&otherCount)&&otherCount==n&&second.close());
  for(size_t i=0;i<n;++i)for(size_t j=0;j<otherCount;++j)assert(list[i]!=other[j]);
  uint8_t mbr[512], boot[512];
  const auto le16=[](const uint8_t* p){return uint16_t(p[0]|uint16_t(p[1])<<8);};
  const auto le32=[&](const uint8_t* p){return uint32_t(le16(p)|uint32_t(le16(p+2))<<16);};
  assert(disk.readSector(0,mbr)&&disk.readSector(le32(mbr+454),boot));
  const uint32_t fatSectors=le16(boot+22);assert(fatSectors);
  const Sector_t fatStart=v.fatStartSector();const uint8_t fatCount=v.fatCount();
  // A premature end, loop, or reserved terminator must not be accepted.
  for(int mode=0;mode<3;++mode){
    Disk broken;broken.sectorsTotal=disk.sectorsTotal;broken.sectors=disk.sectors;
    const Cluster_t which=mode==0?list[0]:list[2];
    const uint16_t value=mode==0?0xffff:mode==1?uint16_t(list[0]):0xfff7;
    const uint32_t offset=which*2;
    for(uint8_t index=0;index<fatCount;++index){auto& sector=broken.sectors[fatStart+index*fatSectors+offset/512];sector[offset%512]=uint8_t(value);sector[offset%512+1]=uint8_t(value>>8);}
    FsVolume check;assert(check.begin(&broken));auto file=check.open("/settings.json",O_RDONLY);assert(file);
    Cluster_t result[64]{};size_t count=999;assert(!file.settingsClusterList(result,64,&count)&&count==999);assert(file.close());
  }
  {
    Disk unreadable;unreadable.sectorsTotal=disk.sectorsTotal;unreadable.sectors=disk.sectors;
    FsVolume check;assert(check.begin(&unreadable));auto file=check.open("/settings.json",O_RDONLY);assert(file);
    unreadable.failReadSector=fatStart+(list[0]*2)/512;
    Cluster_t result[64]{};size_t count=999;assert(!file.settingsClusterList(result,64,&count)&&unreadable.injectedRead&&count==999);assert(file.close());
  }
  {
    Disk cross;cross.sectorsTotal=disk.sectorsTotal;cross.sectors=disk.sectors;
    const uint32_t offset=other[1]*2;
    for(uint8_t index=0;index<fatCount;++index){auto& sector=cross.sectors[fatStart+index*fatSectors+offset/512];sector[offset%512]=uint8_t(list[2]);sector[offset%512+1]=uint8_t(list[2]>>8);}
    FsVolume check;assert(check.begin(&cross));auto left=check.open("/settings.json",O_RDONLY),right=check.open("/staged.json",O_RDONLY);assert(left&&right);
    Cluster_t first[64]{},next[64]{};size_t firstCount=0,nextCount=0;
    assert(left.settingsClusterList(first,64,&firstCount)&&right.settingsClusterList(next,64,&nextCount));
    bool overlaps=false;for(size_t i=0;i<firstCount;++i)for(size_t j=0;j<nextCount;++j)if(first[i]==next[j])overlaps=true;
    assert(overlaps&&!settingsFilesHaveDisjointChains(left,right)&&left.close()&&right.close());
  }
  std::printf("FAT16 cluster=%zu file=%zu checked clusters=%zu\n",clusterBytes,data.size(),n);
  {
    Disk exdisk;uint8_t scratch2[512];ExFatFormatter exformatter;assert(exformatter.format(&exdisk,scratch2));
    FsVolume ex;assert(ex.begin(&exdisk));const size_t bytes=ex.bytesPerCluster();assert(bytes+13<=32768);
    const std::string payload(bytes+13,'x');
    auto source=ex.open("/settings.json",O_CREAT|O_EXCL|O_RDWR);assert(source&&source.write(payload.data(),payload.size())==payload.size()&&source.sync());
    Cluster_t clusters[64]{};size_t count=0;assert(source.settingsClusterList(clusters,64,&count)&&count==2);assert(source.close());
    const uint32_t bit=clusters[1]-2;const Sector_t bitmap=ex.dataStartSector()+bit/4096;
    Disk broken;broken.sectorsTotal=exdisk.sectorsTotal;broken.sectors=exdisk.sectors;broken.sectors[bitmap][(bit/8)%512]&=static_cast<uint8_t>(~(uint8_t{1}<<(bit%8)));
    FsVolume check;assert(check.begin(&broken));auto file=check.open("/settings.json",O_RDONLY);assert(file);
    size_t badCount=999;assert(!file.settingsClusterList(clusters,64,&badCount)&&badCount==999);assert(file.close());
    std::printf("exFAT cluster=%zu file=%zu checked clusters=%zu, free bitmap refused\n",bytes,payload.size(),count);
  }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    project = tmp / 'project'
    sd = project / '.pio/libdeps/x4pro/SdFat'
    shutil.copytree(dep / 'src', sd / 'src')
    shutil.copy2(dep / 'library.properties', sd / 'library.properties')
    shutil.copytree(root / 'scripts/sdfat_patches', project / 'scripts/sdfat_patches')
    patcher.apply_patches(project, sd)
    fat_h = sd / 'src/FatLib/FatFile.h'
    data = fat_h.read_text()
    assert data.count('  bool settingsClusterList(Cluster_t* out, size_t capacity, size_t* count);') == 1
    fat_cpp = sd / 'src/FatLib/FatFile.cpp'
    assert fat_cpp.read_text().count('bool FatFile::settingsClusterList(') == 1
    ex_h = sd / 'src/ExFatLib/ExFatFile.h'
    data = ex_h.read_text()
    assert data.count('  bool settingsClusterList(Cluster_t* out, size_t capacity, size_t* count);') == 1
    ex_cpp = sd / 'src/ExFatLib/ExFatFile.cpp'
    assert ex_cpp.read_text().count('bool ExFatFile::settingsClusterList(') == 1
    # SdFat's formatter normally chooses 128 KiB clusters. Use a small cluster
    # geometry in this isolated formatter copy to exercise multi-cluster exFAT.
    formatter = sd / 'src/ExFatLib/ExFatFormatter.cpp'
    data = formatter.read_text()
    anchor = '  sectorsPerClusterShift = vs < 29 ? 8 : (vs - 11) / 2;'
    assert data.count(anchor) == 1
    formatter.write_text(data.replace(anchor, '  sectorsPerClusterShift = 5;'))
    fs_h = sd / 'src/FsLib/FsFile.h'
    data = fs_h.read_text()
    anchor = '  bool settingsClusterList(Cluster_t* out, size_t capacity, size_t* count) {'
    assert data.count(anchor) == 1
    source = sd / 'src'
    files = []
    for folder in ('FsLib', 'FatLib', 'ExFatLib', 'common'):
        files += sorted((source / folder).glob('*.cpp'))
    cpp = tmp / 'test.cpp'
    cpp.write_text(prefix + body)
    flags = ['-std=c++20', '-O1', '-fsanitize=address', '-fno-omit-frame-pointer', '-DSPI_DRIVER_SELECT=3',
             '-DUSE_BLOCK_DEVICE_INTERFACE=1', '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0',
             '-DENABLE_ARDUINO_SERIAL=0', '-DSDFAT_FILE_TYPE=3', '-DUSE_LONG_FILE_NAMES=1',
             '-DUSE_UTF8_LONG_NAMES=1', '-DDESTRUCTOR_CLOSES_FILE=1', '-DUSE_SEPARATE_FAT_CACHE=1']
    executable = tmp / 'probe'
    command = [os.environ.get('CXX', 'c++'), *flags, '-include', str(root / 'test/sdfat_lfn_lookup/HostCompat.h'),
               '-I', str(source), '-I', str(root / 'lib/Memory'), '-I', str(root / 'test/book_reading_stats_atomic/stubs'), '-I', str(root / 'lib/hal'), str(cpp), *map(str, files), '-o', str(executable)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    subprocess.run([str(executable)], check=True)
