"""Prototype a bounded SdFat small-file identity check in a temporary copy."""
from pathlib import Path
import os

root = Path(os.environ.get('CROSSPOINT_WORKTREE', Path.cwd())).resolve()
source = (root / 'test/test_stats_safe_replace_real.py').read_text()
source = source.replace("ROOT = Path(__file__).resolve().parents[1]",
                        "ROOT = Path(os.environ.get('CROSSPOINT_WORKTREE', Path.cwd())).resolve()")
anchor = '    cpp_files = []\n'
insert = r'''
    declarations = {
        'FatLib/FatFile.h': '  bool smallSingleClusterChain();\n',
        'ExFatLib/ExFatFile.h': '  bool smallSingleClusterChain();\n',
        'FsLib/FsFile.h': '  bool smallSingleClusterChain() { return m_fFile ? m_fFile->smallSingleClusterChain() : m_xFile ? m_xFile->smallSingleClusterChain() : false; }\n',
    }
    for name, declaration in declarations.items():
        file = source / name
        contents = file.read_text()
        point = '  bool contiguousRange(Sector_t* bgnSector, Sector_t* endSector)'
        assert contents.count(point) == 1
        file.write_text(contents.replace(point, declaration + point))
    fat = source / 'FatLib/FatFile.cpp'
    fat.write_text(fat.read_text() + "\nbool FatFile::smallSingleClusterChain() {\n"
                   "  if (!isFile() || !fileSize() || fileSize() > 512 || m_firstCluster < 2) return false;\n"
                   "  Cluster_t next; return m_vol->fatGet(m_firstCluster, &next) == 0;\n}\n")
    exfat = source / 'ExFatLib/ExFatFile.cpp'
    exfat.write_text(exfat.read_text() + "\nbool ExFatFile::smallSingleClusterChain() {\n"
                     "  if (!isFile() || !fileSize() || !m_dataLength || m_dataLength > 512 || m_firstCluster < 2) return false;\n"
                     "  if (isContiguous()) return true;\n"
                     "  Cluster_t next; return m_vol->fatGet(m_firstCluster, &next) == 0;\n}\n")
    local = Path(temporary) / 'bounded'
    local.mkdir()
    (local / 'SmallFileIdentity.h').write_text("""#pragma once
template <typename File> bool smallFilesHaveDifferentFirstClusters(File& a, File& b) {
 if (!a.smallSingleClusterChain() || !b.smallSingleClusterChain()) return false;
 return a.firstCluster() != b.firstCluster();
}
""")
    fixture = (ROOT / 'test/stats_safe_replace_real/RealStatsReplaceTest.cpp').read_text()
    fixture = fixture.replace('  int writes = 0, failAt = -1;',
                              '  Sector_t sectorsTotal = 1024 * 1024;\n  int writes = 0, failAt = -1;')
    fixture = fixture.replace('Sector_t sectorCount() override { return 1024 * 1024; }',
                              'Sector_t sectorCount() override { return sectorsTotal; }')
    fixture = fixture.replace('void exerciseCrosslinkedFatTail(bool backupFirst) {\n  Disk disk;',
                              'void exerciseCrosslinkedFatTail(bool backupFirst, bool fat32) {\n  Disk disk;\n  if (fat32) disk.sectorsTotal = 64 * 1024 * 1024;')
    fixture = fixture.replace('  assert(primaryCluster && backupCluster && primaryCluster != backupCluster);',
                              '  assert(primaryCluster && backupCluster && primaryCluster != backupCluster);\n'
                              '  assert(fatType == (fat32 ? 32 : 16));\n'
                              '  assert(backupFirst ? backupCluster + 1 == primaryCluster : primaryCluster + 1 == backupCluster);')
    fixture = fixture.replace('  exerciseCrosslinkedFatTail(false);\n  exerciseCrosslinkedFatTail(true);',
                              '  exerciseCrosslinkedFatTail(false, false);\n  exerciseCrosslinkedFatTail(true, false);\n'
                              '  exerciseCrosslinkedFatTail(false, true);\n  exerciseCrosslinkedFatTail(true, true);')
    extra = r"""
void exerciseFatChainedExfat() {
 Disk disk;uint8_t scratch[512];ExFatFormatter formatter;assert(formatter.format(&disk,scratch));
 {FsVolume volume;assert(volume.begin(&disk));
  auto file=volume.open("/chained.bin",O_CREAT|O_EXCL|O_RDWR);assert(file);
  uint8_t bytes[73]{};assert(file.write(bytes,sizeof(bytes))==sizeof(bytes));assert(file.sync());
  auto blocker=volume.open("/blocker.bin",O_CREAT|O_EXCL|O_WRONLY);assert(blocker);
  assert(blocker.write(uint8_t{1})==1 && blocker.close());
  const std::string extension(volume.bytesPerCluster(),'x');
  assert(file.write(extension.data(),extension.size())==extension.size());
  assert(!file.isContiguous());assert(file.truncate(sizeof(bytes)));assert(file.close());}
 FsVolume after;assert(after.begin(&disk));auto file=after.open("/chained.bin",O_RDONLY);assert(file);
 assert(!file.isContiguous());assert(file.smallSingleClusterChain());assert(file.close());
 std::puts("exFAT one-cluster FAT-chain accepted");
}
"""
    fixture = fixture.replace('int main() {', extra + '\nint main() {', 1)
    fixture = fixture.replace('  exerciseCrosslinkedFatTail(true, true);',
                              '  exerciseCrosslinkedFatTail(true, true);\n  exerciseFatChainedExfat();')
    (local / 'RealStatsReplaceTest.cpp').write_text(fixture)
'''
assert source.count(anchor) == 1
source = source.replace(anchor, insert + anchor)
source = source.replace("'-I', str(ROOT / 'lib/hal'), '-I', str(source)",
                        "'-I', str(local), '-I', str(ROOT / 'lib/hal'), '-I', str(source)")
source = source.replace("str(ROOT / 'test/stats_safe_replace_real/RealStatsReplaceTest.cpp')",
                        "str(local / 'RealStatsReplaceTest.cpp')")
exec(compile(source, str(root / 'test/test_stats_safe_replace_real.py'), 'exec'), {'__name__': '__main__'})
