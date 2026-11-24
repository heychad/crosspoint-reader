"""Verify Saved Items absence checks against real FAT/exFAT lookups."""

import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('saved_items_hal', ROOT / 'test/storage_fence_integration/test_hal_real.py')
hal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hal)
code = hal.code[:hal.code.index('\n#include "StorageDirectoryScan.h"')]
lookup_stub = 'PathStatus lookupPath(const char* path){auto file=SDCard.open(path);if(!file)return PathStatus::Missing;return file.close()?PathStatus::Present:PathStatus::Failed;}'
assert lookup_stub in code
code = code.replace(lookup_stub, 'PathStatus lookupPath(const char*);')
code = code.replace('bool close();bool isOpen()const;', 'bool close();bool isOpen()const;bool isDirectory()const;')
code = code.replace('HalFile openChecked(const char*,oflag_t);', 'HalFile openChecked(const char*,oflag_t=O_RDONLY);')
code += '\n#include "CheckedStoragePath.h"\n' + hal.extract('HalStorage::PathStatus HalStorage::lookupPath(')
code += r'''
bool HalFile::isDirectory() const { return impl && impl->file.isDirectory(); }
HalStorage& Storage = HalStorage::getInstance();
#include "util/SavedItemsCandidates.cpp"

using bookmove::BookKind;
using bookmove::Root;
using saveditems::Candidates;

auto path(std::string_view book, Root root) { return bookmove::rootPath(book, BookKind::Reflowable, root); }

void finish(Candidates& scan) {
  for (int i = 0; i < 100; ++i) {
    if (scan.advance()) return;
  }
  assert(false && "directory scan did not finish");
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const bool exfat = std::string_view(argv[1]) == "exfat";
  Disk disk;
  uint8_t scratch[512];
  if (exfat) { ExFatFormatter format; assert(format.format(&disk, scratch)); }
  else { FatFormatter format; assert(format.format(&disk, scratch)); }
  FsVolume volume;
  assert(volume.begin(&disk));
  SDCard.volume = &volume;
  assert(volume.mkdir("/.crosspoint", true));
  constexpr char native[] = "/Books/One.epub";
  constexpr char badNative[] = "/Books/Unreadable.epub";
  constexpr char crc[] = "/Books/Two.epub";
  constexpr char hash[] = "/Books/Three.epub";
  constexpr char clip[] = "/Books/Four.epub";
  constexpr char absent[] = "/Books/Missing.epub";
  Candidates missing;
  finish(missing);
  assert(missing.valid() && missing.empty() && missing.kindsFor(native) == 0);
  assert(volume.mkdir("/.crosspoint/bookmarks", true));
  assert(volume.mkdir("/.crosspoint/clippings", true));
  Candidates empty;
  finish(empty);
  assert(empty.valid() && empty.empty());
  auto put = [](const std::string& name) {
    auto file = Storage.openChecked(name.c_str(), O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(uint8_t{1}) == 1 && file.close());
  };
  put("/.crosspoint/bookmarks/BOOKS_ONE.JSON");
  assert(!empty.valid());
  assert(Storage.lookupPath(path(native, Root::NativeBookmarks).c_str()) == HalStorage::PathStatus::Present);
  assert(volume.mkdir(path(badNative, Root::NativeBookmarks).c_str()));
  put(path(crc, Root::CrcBookmarks) + ".bookmark-edit.bak");
  put(path(hash, Root::HashBookmarks) + ".bookmark-edit.guard");
  put(path(clip, Root::Clippings) + ".bak");
  Candidates sparse;
  finish(sparse);
  assert(sparse.valid() && !sparse.empty());
  assert(sparse.kindsFor(native) & saveditems::BOOKMARKS);
  assert(sparse.kindsFor(badNative) & saveditems::BOOKMARKS);
  assert(sparse.kindsFor(crc) & saveditems::CROSSINK_BOOKMARKS);
  assert(sparse.kindsFor(hash) & saveditems::CROSSINK_BOOKMARKS);
  assert(sparse.kindsFor(clip) & saveditems::HAS_CLIPPINGS);
  unsigned possible = 0;
  for (int i = 0; i < 4096; ++i) {
    possible += sparse.kindsFor("/Books/Catalog " + std::to_string(i) + ".epub") != 0;
  }
  assert(possible < 64);
  // A Unicode filename must not force a filesystem lookup for every book.
  put("/.crosspoint/bookmarks/Café.json");
  const std::string accented = "/Books/A long Étude for two voices.epub";
  put(path(accented, Root::NativeBookmarks));
  const std::string astral = "/Books/Astral 🍎 reading notes.epub";
  put(BookmarkUtil::nativeRecoveryPath(path(astral, Root::NativeBookmarks), true));
  const std::string longBook = "/" + std::string(248, 'x') + "🍎.epub";
  auto longBackup = BookmarkUtil::nativeRecoveryPath(path(longBook, Root::NativeBookmarks), true);
  assert(longBackup.ends_with(".jbak"));
  put(longBackup);
  // This pair in the pinned SdFat upcase table uses two and three UTF-8 bytes.
  put("/.crosspoint/bookmarks/Books_long_ɫetter_pair.json");
  Candidates unicode;
  finish(unicode);
  assert(unicode.valid());
  assert(unicode.kindsFor(accented) & saveditems::BOOKMARKS);
  const auto folded = "/Books/A LONG étude for two voices.epub";
  assert(Storage.lookupPath(path(folded, Root::NativeBookmarks).c_str()) == HalStorage::PathStatus::Present);
  assert(unicode.kindsFor(folded) & saveditems::BOOKMARKS);
  const auto differentWidthFold = "/Books/long_Ɫetter_pair.epub";
  assert(Storage.lookupPath(path(differentWidthFold, Root::NativeBookmarks).c_str()) == HalStorage::PathStatus::Present);
  assert(unicode.kindsFor(differentWidthFold) & saveditems::BOOKMARKS);
  assert(unicode.kindsFor(astral) & saveditems::BOOKMARKS);
  assert(unicode.kindsFor(longBook) & saveditems::BOOKMARKS);
  assert(unicode.kindsFor("/Library/A title with an unmatched length in this inventory.epub") == 0);
  assert(unicode.kindsFor(crc) & saveditems::CROSSINK_BOOKMARKS);
  assert(unicode.kindsFor(hash) & saveditems::CROSSINK_BOOKMARKS);
  assert(!(unicode.kindsFor(absent) & saveditems::HAS_CLIPPINGS));

  SDCard.volume = nullptr;
  volume.end();
  assert(volume.begin(&disk));
  SDCard.volume = &volume;
  Candidates fault;
  disk.failReadAt = disk.reads;
  finish(fault);
  assert(disk.injectedRead && !fault.valid());
  SDCard.volume = nullptr;
  volume.end();
}
'''

with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(code)
    (tmp / 'HalStorage.h').write_text('#pragma once\n')
    (tmp / 'Memory.h').write_text('#pragma once\n')
    (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long tick=1;return tick++;}\n')
    includes = [tmp, ROOT / 'lib/hal', ROOT / 'lib/Serialization', ROOT / 'src', hal.dependency / 'src']
    command = [os.environ.get('CXX', 'c++'), *hal.flags, '-U__ARM_FEATURE_UNALIGNED',
               *shlex.split(os.environ.get('CXXFLAGS', '')), '-include', str(tmp / 'HostCompat.h'),
               *(flag for path in includes for flag in ('-I', str(path))), str(tmp / 'test.cpp'),
               str(ROOT / 'src/util/BookmarkUtil.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
    subprocess.run(command, check=True)
    for filesystem in ('fat', 'exfat'):
        subprocess.run([str(tmp / 'test'), filesystem], check=True)
print('Saved Items correctly narrows checked lookup on FAT and exFAT')
