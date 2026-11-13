"""Fault-test completion journal writes with the patched SdFat driver."""
from pathlib import Path
import importlib.util
import hashlib
import os
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCY = Path(os.environ.get('SDFAT_SOURCE', ROOT / '.pio/libdeps/x4pro/SdFat'))
spec = importlib.util.spec_from_file_location('sdfat_patch', ROOT / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
with tempfile.TemporaryDirectory() as temporary:
    project = Path(temporary) / 'project'
    dependency = project / '.pio/libdeps/x4pro/SdFat'
    shutil.copytree(DEPENDENCY / 'src', dependency / 'src')
    shutil.copy2(DEPENDENCY / 'library.properties', dependency / 'library.properties')
    shutil.copytree(ROOT / 'scripts/sdfat_patches', project / 'scripts/sdfat_patches')
    patcher.apply_patches(project, dependency)
    source = dependency / 'src'
    wrapper = source / 'FsLib/FsFile.h'
    expected = next(row[3] for row in patcher.PATCHES if row[1] == 'src/FsLib/FsFile.h')
    patches = project / 'scripts/sdfat_patches'
    for name in ('0008-fsfile-strict-lookup.patch', '0014-fsfile-bounded-chain.patch',
                 '0018-fsfile-prior-bounded-chain.patch', '0021-fs-header-settings-cluster.patch'):
        if name != '0008-fsfile-strict-lookup.patch':
            subprocess.run(['git', 'apply', '--reverse', str(patches / '0038-fs-move-entry.patch')], cwd=dependency, check=True)
        subprocess.run(['git', 'apply', '--reverse', str(patches / name)], cwd=dependency, check=True)
        patcher.apply_patches(project, dependency)
        assert hashlib.sha256(wrapper.read_bytes()).hexdigest() == expected
    # Isolated research only: mutate the copied dependency, never the SDK or
    # production patch set. It is not enabled in firmware.
    alias_experiment = os.environ.get('SDFAT_ALIAS_EXPERIMENT') == '1'
    large_experiment = alias_experiment and os.environ.get('SDFAT_ALIAS_LARGE') == '1'
    atomic_experiment = large_experiment and os.environ.get('SDFAT_ALIAS_ONE_SECTOR') == '1'
    cleanup_experiment = os.environ.get('SDFAT_CLEANUP_EXPERIMENT') == '1'
    snapshot_experiment = os.environ.get('SDFAT_IMPORT_SNAPSHOT') == '1'
    transaction_experiment = os.environ.get('SDFAT_IMPORT_TRANSACTION') == '1'
    inplace_cleanup = cleanup_experiment and os.environ.get('SDFAT_CLEANUP_INPLACE') == '1'
    short_journal = cleanup_experiment and os.environ.get('SDFAT_SHORT_JOURNAL') == '1'
    if short_journal and alias_experiment:
        raise ValueError('Short-name cleanup research and alias research run separately')
    if alias_experiment:
        for relative, kind in [('FatLib/FatFile.h', 'FatFile'), ('ExFatLib/ExFatFile.h', 'ExFatFile')]:
            header = source / relative
            contents = header.read_text()
            location_check = ('m_dirSector == keep.m_dirSector && m_dirIndex == keep.m_dirIndex'
                              if kind == 'FatFile' else
                              'm_dirPos.cluster == keep.m_dirPos.cluster && m_dirPos.position == keep.m_dirPos.position')
            extra_check = ('' if kind == 'FatFile' else
                           ' || isContiguous() != keep.isContiguous() || validLength() != keep.validLength()')
            method = f'''  bool experimentUnlinkAlias({kind}& keep) {{
    if (!isFile() || !keep.isFile() || !isWritable() || m_vol != keep.m_vol ||
        !firstCluster() || firstCluster() != keep.firstCluster() || fileSize() != keep.fileSize() ||
        ({location_check}){extra_check}) return false;
    Cluster_t a[64], b[64]; size_t na = 0, nb = 0;
    if (!settingsClusterList(a, 64, &na) || !keep.settingsClusterList(b, 64, &nb) || na != nb || !na) return false;
    for (size_t i = 0; i < na; ++i) if (a[i] != b[i]) return false;
    {kind} entry; entry.copy(this); entry.m_firstCluster = 0;
    entry.m_flags = FILE_FLAG_WRITE; entry.m_attributes = FILE_ATTR_FILE;
    const bool removed = entry.remove();
    entry.m_flags = 0; entry.m_attributes = FILE_ATTR_CLOSED;
    return removed;
  }}
'''
            if large_experiment:
                method = f'  bool experimentChain();\n  bool experimentUnlinkAlias({kind}& keep);\n'
                if atomic_experiment and kind == 'FatFile':
                    method += '  bool experimentUnlinkOneSector();\n'
                    method += '  bool experimentCrossesSector() const;\n'
                    method += '''  struct ExperimentLocator {
    uint32_t parentCluster, firstCluster, fileSize;
    uint16_t directoryIndex;
    uint8_t lfnCount;
    uint8_t entries[21][32];
  };
  bool experimentCaptureLocator(FatFile& keep, ExperimentLocator& locator);
  bool experimentRecoverLocator(FatFile& keep, const ExperimentLocator& locator);
'''
                implementation = source / ('FatLib/FatFile.cpp' if kind == 'FatFile' else 'ExFatLib/ExFatFile.cpp')
                per_cluster = '''
      Cluster_t next = 0; bool strictEoc = false;
      const int status = m_vol->fatGet(current, &next, &strictEoc);
      if (i + 1 == expected) { if (status != 0 || !strictEoc) return false; }
      else { if (status != 1) return false; current = next; }
''' if kind == 'FatFile' else '''
      const uint32_t bit = current - 2;
      const uint8_t sectorShift = m_vol->bytesPerSectorShift();
      if (sectorShift >= 29) return false;
      const Sector_t sector = m_vol->clusterHeapStartSector() + (bit >> (sectorShift + 3));
      const uint8_t* bits = m_vol->bitmapCachePrepare(sector, FsCache::CACHE_FOR_READ);
      if (!bits || !(bits[(bit >> 3) & ((uint32_t{1} << sectorShift) - 1)] & (uint8_t{1} << (bit & 7)))) return false;
      if (isContiguous()) {
        if (i + 1 < expected) { if (current == UINT32_MAX) return false; ++current; }
      } else {
        Cluster_t next = 0; const int status = m_vol->fatGet(current, &next);
        if (i + 1 == expected) { if (status != 0) return false; }
        else { if (status != 1) return false; current = next; }
      }
'''
                remove_body = '''
  return experimentUnlinkOneSector();
''' if atomic_experiment and kind == 'FatFile' else f'''
  {kind} entry; entry.copy(this); entry.m_firstCluster = 0;
  entry.m_flags = FILE_FLAG_WRITE; entry.m_attributes = FILE_ATTR_FILE;
  const bool removed = entry.remove();
  entry.m_flags = 0; entry.m_attributes = FILE_ATTR_CLOSED;
  return removed;
'''
                body = f'''
bool {kind}::experimentChain() {{
  if (!isFile() || !fileSize() || firstCluster() < 2) return false;
  const uint8_t shift = m_vol->bytesPerClusterShift();
  if (shift >= 63) return false;
  const uint64_t expected = 1 + (uint64_t(fileSize()) - 1) / (uint64_t{{1}} << shift);
  if (!expected || expected > m_vol->clusterCount()) return false;
  Cluster_t current = firstCluster();
  for (uint64_t i = 0; i < expected; ++i) {{
    if (current < 2 || current - 2 >= m_vol->clusterCount()) return false;
{per_cluster}
  }}
  return true;
}}
bool {kind}::experimentUnlinkAlias({kind}& keep) {{
  if (!isFile() || !keep.isFile() || !isWritable() || m_vol != keep.m_vol ||
      !firstCluster() || firstCluster() != keep.firstCluster() || fileSize() != keep.fileSize() ||
      ({location_check}){extra_check}) return false;
  if (!experimentChain() || !keep.experimentChain()) return false;
{remove_body}
}}
'''
                if atomic_experiment and kind == 'FatFile':
                    body += '''
bool FatFile::experimentCrossesSector() const {
  const uint8_t shift = m_vol->bytesPerSectorShift();
  return shift >= 5 && shift < 16 && m_lfnOrd > m_dirIndex % (uint32_t{1} << (shift - 5));
}
bool FatFile::experimentCaptureLocator(FatFile& keep, ExperimentLocator& locator) {
  if (!isFile() || !keep.isFile() || m_vol != keep.m_vol || !firstCluster() ||
      firstCluster() != keep.firstCluster() || fileSize() != keep.fileSize() ||
      (m_dirSector == keep.m_dirSector && m_dirIndex == keep.m_dirIndex) ||
      !experimentChain() || !keep.experimentChain() || m_lfnOrd > 20 || m_dirIndex < m_lfnOrd) return false;
  FatFile parent;
  if (!parent.openCluster(this)) return false;
  ExperimentLocator next{};
  next.parentCluster = parent.firstCluster();
  next.firstCluster = firstCluster(); next.fileSize = fileSize();
  next.directoryIndex = m_dirIndex; next.lfnCount = m_lfnOrd;
  bool valid = true;
  for (uint8_t i = 0; i <= m_lfnOrd; ++i) {
    const auto* entry = parent.cacheDir(m_dirIndex - i);
    if (!entry) { valid = false; break; }
    memcpy(next.entries[i], entry, 32);
  }
  const bool closed = parent.close();
  if (!valid || !closed) return false;
  const auto* shortEntry = reinterpret_cast<const DirFat_t*>(next.entries[0]);
  if (!isFatFileOrSubdir(shortEntry) || shortEntry->name[0] == FAT_NAME_DELETED ||
      shortEntry->name[0] == FAT_NAME_FREE) return false;
  const uint8_t checksum = lfnChecksum(shortEntry->name);
  for (uint8_t i = 1; i <= m_lfnOrd; ++i) {
    const auto* lfn = reinterpret_cast<const DirLfn_t*>(next.entries[i]);
    const bool last = (lfn->order & FAT_ORDER_LAST_LONG_ENTRY) != 0;
    if (lfn->attributes != FAT_ATTRIB_LONG_NAME || (lfn->order & 0x1f) != i || lfn->checksum != checksum ||
        last != (i == m_lfnOrd)) return false;
  }
  locator = next;
  return true;
}
bool FatFile::experimentRecoverLocator(FatFile& keep, const ExperimentLocator& locator) {
  if (!isDir() || !keep.isFile() || m_vol != keep.m_vol || firstCluster() != locator.parentCluster ||
      !locator.firstCluster || keep.firstCluster() != locator.firstCluster || keep.fileSize() != locator.fileSize ||
      locator.lfnCount > 20 || locator.directoryIndex < locator.lfnCount || !keep.experimentChain()) return false;
  // Compare every full entry before writing; only the deletion marker is
  // allowed to differ. Reused or edited directory slots cannot be removed.
  for (uint8_t i = 0; i <= locator.lfnCount; ++i) {
    const auto* entry = reinterpret_cast<const uint8_t*>(cacheDir(locator.directoryIndex - i));
    if (!entry || (entry[0] != locator.entries[i][0] && entry[0] != FAT_NAME_DELETED) ||
        memcmp(entry + 1, locator.entries[i] + 1, 31)) return false;
  }
  for (uint8_t i = 0; i <= locator.lfnCount; ++i) {
    auto* entry = reinterpret_cast<uint8_t*>(cacheDir(locator.directoryIndex - i));
    if (!entry) return false;
    entry[0] = FAT_NAME_DELETED;
    m_vol->cacheDirty();
  }
  return m_vol->cacheSync();
}
bool FatFile::experimentUnlinkOneSector() {
  const uint8_t shift = m_vol->bytesPerSectorShift();
  if (shift < 5 || shift >= 16) return false;
  const uint32_t entries = uint32_t{1} << (shift - 5);
  const uint32_t index = m_dirIndex % entries;
  if (m_lfnOrd > 20 || index < m_lfnOrd) return false;
  uint8_t* raw = m_vol->dataCachePrepare(m_dirSector, FsCache::CACHE_FOR_WRITE);
  if (!raw) return false;
  auto* dir = reinterpret_cast<DirFat_t*>(raw) + index;
  if (dir->name[0] == FAT_NAME_DELETED || dir->name[0] == FAT_NAME_FREE || !isFatFileOrSubdir(dir)) return false;
  const uint8_t checksum = lfnChecksum(dir->name);
  for (uint8_t order = 1; order <= m_lfnOrd; ++order) {
    const auto* lfn = reinterpret_cast<const DirLfn_t*>(raw + (index - order) * FS_DIR_SIZE);
    const bool last = (lfn->order & FAT_ORDER_LAST_LONG_ENTRY) != 0;
    if (lfn->attributes != FAT_ATTRIB_LONG_NAME || (lfn->order & 0x1f) != order ||
        lfn->checksum != checksum || last != (order == m_lfnOrd)) return false;
  }
  dir->name[0] = FAT_NAME_DELETED;
  for (uint8_t order = 1; order <= m_lfnOrd; ++order)
    reinterpret_cast<DirLfn_t*>(raw + (index - order) * FS_DIR_SIZE)->order = FAT_NAME_DELETED;
  return m_vol->cacheSync();
}
'''
                implementation.write_text(implementation.read_text() + body)
            assert contents.count('  bool remove();') == 1
            header.write_text(contents.replace('  bool remove();', method + '  bool remove();', 1))
        header = source / 'FsLib/FsFile.h'
        contents = header.read_text()
        method = '''  bool experimentUnlinkAlias(FsBaseFile& keep) {
    return m_fFile && keep.m_fFile ? m_fFile->experimentUnlinkAlias(*keep.m_fFile) :
           m_xFile && keep.m_xFile && m_xFile->experimentUnlinkAlias(*keep.m_xFile);
  }
'''
        if atomic_experiment:
            method += '''  bool experimentCrossesSector() const { return m_fFile && m_fFile->experimentCrossesSector(); }
  using ExperimentLocator = FatFile::ExperimentLocator;
  bool experimentCaptureLocator(FsBaseFile& keep, ExperimentLocator& locator) {
    return m_fFile && keep.m_fFile && m_fFile->experimentCaptureLocator(*keep.m_fFile, locator);
  }
  bool experimentRecoverLocator(FsBaseFile& keep, const ExperimentLocator& locator) {
    return m_fFile && keep.m_fFile && m_fFile->experimentRecoverLocator(*keep.m_fFile, locator);
  }
'''
        assert contents.count('  bool remove();') == 1
        header.write_text(contents.replace('  bool remove();', method + '  bool remove();', 1))
    # Compile the actual production batch lookup, substituting only its SDCard
    # directory-open adapter and the test lock implementation.
    hal_source = (ROOT / 'lib/hal/HalStorage.cpp').read_text()
    start = hal_source.index('bool HalStorage::lookupNumberedChildren(')
    end = hal_source.index('\nbool HalStorage::distinctFirstClusters(', start)
    method = hal_source[start:end].replace('auto directory = SDCard.open(path);',
                                          'auto directory = openBatchDirectory(path);')
    path_start = hal_source.index('HalStorage::PathStatus HalStorage::lookupPath(')
    path_end = hal_source.index('\nbool HalStorage::withStorageLock(', path_start)
    path_method = hal_source[path_start:path_end].replace('!SDCard.ready() || ', '').replace(
        'SDCard.open("/")', 'openBatchRoot()')
    production_lookup = Path(temporary) / 'ProductionBatchLookup.cpp'
    production_lookup.write_text('#include <HalStorage.h>\n#include <CheckedStoragePath.h>\n'
                                 '#include <cstdio>\n#include <cstring>\n#include <Memory.h>\n' + path_method + '\n' + method + '\n')
    cpp_files = []
    for folder in ('FsLib', 'FatLib', 'ExFatLib', 'common'):
        cpp_files.extend(sorted((source / folder).glob('*.cpp')))
    flags = ['-std=c++20', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
             '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
             '-DSDFAT_FILE_TYPE=3', '-DUSE_LONG_FILE_NAMES=1', '-DUSE_UTF8_LONG_NAMES=1',
             '-DDESTRUCTOR_CLOSES_FILE=1', '-DUSE_SEPARATE_FAT_CACHE=1']
    payload_size = os.environ.get('STATS_TEST_PAYLOAD', '73')
    if payload_size not in ('73', '159'):
        raise ValueError('Unsupported statistics test payload size')
    flags.append(f'-DSTATS_PAYLOAD_SIZE={payload_size}')
    if alias_experiment:
        flags.append('-DSDFAT_ALIAS_EXPERIMENT=1')
    if large_experiment:
        flags.append('-DSDFAT_ALIAS_LARGE=1')
    if atomic_experiment:
        flags.append('-DSDFAT_ALIAS_ONE_SECTOR=1')
    if cleanup_experiment:
        flags.append('-DSDFAT_CLEANUP_EXPERIMENT=1')
    if snapshot_experiment:
        flags.append('-DSDFAT_IMPORT_SNAPSHOT=1')
    if transaction_experiment:
        flags.append('-DSDFAT_IMPORT_TRANSACTION=1')
    if inplace_cleanup:
        flags.append('-DSDFAT_CLEANUP_INPLACE=1')
    if short_journal:
        flags.append('-DSDFAT_SHORT_JOURNAL=1')
    executable = Path(temporary) / 'wrapper'
    lease_stub = Path(temporary) / 'StatsLeaseStub.cpp'
    lease_stub.write_text('#include "StatsSessionLease.h"\nbool StatsSessionLease::anyActive() { return false; }\n')
    journal_cpp = ROOT / 'src/activities/reader/CompletionJournalIO.cpp'
    real_test_cpp = ROOT / 'test/completion_journal_real/CompletionJournalRealTest.cpp'
    if short_journal:
        # Research-only filenames: uppercase 8.3 names, avoiding FAT long-name
        # directory entries. Modify copies only; production names are unchanged.
        def short_names(contents):
            for old, new in [('.completion-', 'C'), ('.cseal-', 'S'), ('.cretire-', 'R'),
                             ('.cclean-', 'K'), ('.cmove-', 'M'), ('.cmoveseal-', 'T')]:
                contents = contents.replace('/.completion-journal/' + old, '/.completion-journal/' + new)
                contents = contents.replace('"' + old + '"', '"' + new + '"')
            return contents
        journal_cpp = Path(temporary) / 'CompletionJournalShort.cpp'
        journal_cpp.write_text(short_names((ROOT / 'src/activities/reader/CompletionJournalIO.cpp').read_text()))
        real_test_cpp = Path(temporary) / 'CompletionJournalShortTest.cpp'
        real_test_cpp.write_text(short_names((ROOT / 'test/completion_journal_real/CompletionJournalRealTest.cpp').read_text()))
        (Path(temporary) / 'CleanupExperiment.h').write_text(
            short_names((ROOT / 'test/completion_journal_real/CleanupExperiment.h').read_text()))
        if inplace_cleanup:
            (Path(temporary) / 'CleanupInplaceExperiment.h').write_text(
                short_names((ROOT / 'test/completion_journal_real/CleanupInplaceExperiment.h').read_text()))
    host_compat = Path(temporary) / 'HostCompat.h'
    host_compat.write_text('''#include <cstdint>
class __FlashStringHelper;
#if defined(SDFAT_REAL_MOVE_STORES)
inline uint32_t liveTestMillis = 0;
inline bool liveTestClockEnabled = false;
inline unsigned long millis() {
  static unsigned long now = 0;
  return liveTestClockEnabled ? liveTestMillis++ : ++now;
}
#else
inline unsigned long millis() { static unsigned long now = 0; return ++now; }
#endif
''')
    real_move_stores = os.environ.get('SDFAT_REAL_MOVE_STORES') == '1'
    stub_directory = ROOT / 'test/completion_journal_real/stubs'
    store_sources = []
    store_includes = []
    probe_sources = [str(ROOT / 'test/completion_journal_real/CompletionMoveProbeStub.cpp')]
    lease_sources = [str(lease_stub)]
    if real_move_stores:
        flags += ['-DSDFAT_REAL_MOVE_STORES=1', '-DCONFIG_IDF_TARGET_ESP32S3=1']
        if os.environ.get('SDFAT_LIVE_COMPLETION') == '1':
            flags.append('-DSDFAT_LIVE_COMPLETION=1')
        stub_directory = Path(temporary) / 'real-store-stubs'
        shutil.copytree(ROOT / 'test/completion_journal_real/stubs', stub_directory)
        (stub_directory / 'CrossPointState.h').unlink()
        (stub_directory / 'RecentBooksStore.h').unlink()
        (stub_directory / 'network/sync/StoredReferenceGuard.h').unlink()
        arduino = (ROOT / 'test/book_reader_settings/stubs/Arduino.h').read_text()
        # HostCompat already provides the test clock; reuse the existing String shim.
        arduino = arduino.replace('inline uint32_t millis() { return hostMillis; }\n', '')
        (stub_directory / 'Arduino.h').write_text(arduino)
        shutil.copy2(ROOT / 'test/book_reader_settings/stubs/ObfuscationUtils.h', stub_directory)
        reader = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        progress_start = reader.index('  HalFile f;\n  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {')
        brace_start = reader.index('{', progress_start)
        depth = 1
        progress_end = brace_start + 1
        while depth:
            if reader[progress_end] == '{':
                depth += 1
            elif reader[progress_end] == '}':
                depth -= 1
            progress_end += 1
        # Execute the reader's progress-load block with only the cache provider replaced.
        (stub_directory / 'MovedProgressLoader.h').write_text(r'''
#pragma once
#include <HalStorage.h>
#include <Logging.h>
#include <optional>
#include <string>
struct MovedProgress { int spine, cachedSpine, page, pages; std::optional<uint32_t> offset; };
inline MovedProgress readMovedProgress(const std::string& cache) {
  struct Book { const std::string& path; const std::string& getCachePath() const {return path;} } book{cache};
  const auto* epub=&book;
  int currentSpineIndex=0, cachedSpineIndex=0, nextPageNumber=0, cachedChapterTotalPageCount=0;
  std::optional<uint32_t> cachedVisibleTextOffset;
''' + reader[progress_start:progress_end] + r'''
  return {currentSpineIndex,cachedSpineIndex,nextPageNumber,cachedChapterTotalPageCount,cachedVisibleTextOffset};
}
''')
        store_sources = [str(ROOT / 'lib/Serialization/PersistableStore.cpp'),
                         str(ROOT / 'src/CrossPointState.cpp'), str(ROOT / 'src/RecentBooksStore.cpp'),
                         str(ROOT / 'src/network/sync/StoredReferenceGuard.cpp'),
                         str(ROOT / 'src/util/CrossInkBookmarks.cpp'),
                         str(ROOT / 'src/util/BookmarkFile.cpp'), str(ROOT / 'src/util/BookmarkUtil.cpp'),
                         str(ROOT / 'src/util/SavedItems.cpp'),
                         str(ROOT / 'src/ClippingStore.cpp'), str(ROOT / 'lib/Utf8/Utf8.cpp'),
                         str(ROOT / 'freeink-sdk/libs/network/CatalogList/src/JsonListParser.cpp'),
                         str(ROOT / 'freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp')]
        # Exercise the transaction identity check and real session ownership,
        # rather than declaring every moved destination a matching transaction.
        store_sources += [str(ROOT / 'src/activities/reader' / name) for name in (
            'CompletionTransaction.cpp', 'CompletionEdit.cpp', 'BookReadingStats.cpp',
            'GlobalReadingStats.cpp', 'ReadingStatsStore.cpp', 'BookTracking.cpp',
            'StatsSessionLease.cpp', 'StatsAccessGate.cpp', 'ReaderStats.cpp',
            'ReadingStatsSession.cpp', 'ReadingSessionStats.cpp', 'ReadingStatsDateEdit.cpp',
            'BookStatsBackup.cpp', 'DeviceStatsBackup.cpp', 'BookCompletion.cpp',
            'DeferredCompletionMove.cpp', 'DeferredCompletionExecutor.cpp', 'DeferredCompletionRecovery.cpp')]
        if os.environ.get('SDFAT_LIVE_DAILY') == '1':
            flags.append('-DCROSSPOINT_EXPERIMENTAL_DAILY_STATS=1')
            store_sources += [str(ROOT / 'src/activities/reader' / name) for name in (
                'DailyReadingStatsSession.cpp', 'DailyReadingStatsStore.cpp')]
        probe_sources = []
        lease_sources = []
        store_includes = ['-I', str(ROOT / 'src'), '-I', str(ROOT / 'lib/Memory'),
                          '-I', str(ROOT / 'test/daily_reading_stats/stubs'),
                          '-I', str(ROOT / 'test/clipping_store/stubs'), '-I', str(ROOT / 'lib/Utf8'),
                          '-I', str(ROOT / 'freeink-sdk/libs/network/CatalogList/include'),
                          '-I', str(ROOT / 'freeink-sdk/libs/network/JsonSax/include'),
                          '-I', str(ROOT / 'test/book_reading_stats_atomic/stubs'),
                          '-I', str(ROOT / 'lib/FsHelpers'),
                          '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src')]
    command = [os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(host_compat), '-I', str(stub_directory), *store_includes, '-I', str(ROOT / 'lib/Memory'), '-I', str(ROOT / 'lib/Serialization'), '-I', str(ROOT / 'test/book_reading_stats_atomic/stubs'), '-I', str(ROOT / 'src/activities/reader'), '-I', str(ROOT / 'lib/hal'), '-I', str(source),
               str(real_test_cpp), str(journal_cpp), str(ROOT / 'test/CompletionArchiveInventoryStub.cpp'), *probe_sources, str(ROOT / 'lib/Serialization/MoveReferenceSafeWrite.cpp'), str(ROOT / 'src/activities/reader/CompletionLibraryDirty.cpp'), str(ROOT / 'src/activities/reader/DeferredCompletionIntentIO.cpp'), str(ROOT / 'src/activities/reader/DeferredCompletionMoveRetirement.cpp'), str(ROOT / 'src/activities/reader/CompletionMoveReferences.cpp'), str(ROOT / 'src/activities/reader/CompletionMoveFile.cpp'), str(ROOT / 'src/activities/reader/CompletionMovePlanIO.cpp'), str(ROOT / 'src/activities/reader/CrossInkBookmarkMove.cpp'), str(ROOT / 'src/activities/reader/StatsSafeReplace.cpp'), str(ROOT / 'src/activities/reader/ReadingStatsUtils.cpp'), *lease_sources, str(production_lookup), *map(str, cpp_files),
               '-o', str(executable)]
    command += store_sources
    if snapshot_experiment or transaction_experiment:
        command += [str(ROOT / 'src/personalization/CrossInkImportSnapshot.cpp'), '-I',
                    str(ROOT / 'src/personalization')]
    if transaction_experiment:
        command += ['-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'), '-I', str(ROOT / 'lib/Serialization'),
                    '-I', str(ROOT / 'src'),
                    str(ROOT / 'src/personalization/CrossInkSettingsSource.cpp'),
                    str(ROOT / 'src/personalization/CrossInkImportGuard.cpp'),
                    str(ROOT / 'src/personalization/CrossInkImportTransaction.cpp'),
                    str(ROOT / 'lib/Serialization/SettingsSafeWrite.cpp'),
                    str(ROOT / 'lib/Serialization/SettingsRecoveryProbe.cpp')]
    compiled = subprocess.run(command, capture_output=True, text=True)
    if compiled.returncode:
        raise AssertionError(compiled.stdout + compiled.stderr)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    if result.returncode or 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
        raise AssertionError(result.stdout + result.stderr + f'\nexit={result.returncode}')
    print(result.stdout.strip())
