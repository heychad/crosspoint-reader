"""Exercise the upload-open policy with production HAL and patched FAT/exFAT."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('upload_hal', ROOT / 'test/storage_fence_integration/test_hal_real.py')
hal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hal)
source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
start = source.index('static bool openUploadFile(')
end = source.index('\n}\n', start) + 2
assert 'openUploadFile(uploadWritePath(state), state.staged, state.file)' in source[source.index('void CrossPointWebServer::handleUpload('):]
code = hal.code[:hal.code.index('\n#include "StorageDirectoryScan.h"')]
lookup_stub = 'PathStatus lookupPath(const char* path){auto file=SDCard.open(path);if(!file)return PathStatus::Missing;return file.close()?PathStatus::Present:PathStatus::Failed;}'
assert lookup_stub in code
code = code.replace(lookup_stub, 'PathStatus lookupPath(const char*);bool withStorageLock(bool (*)(void*),void*);')
code += '\n#include "CheckedStoragePath.h"\n' + hal.extract('HalStorage::PathStatus HalStorage::lookupPath(')
code += '\n' + hal.extract('bool HalStorage::withStorageLock(')
code += '\nusing String = std::string;\nHalStorage& Storage = HalStorage::getInstance();\n' + source[start:end]
code += r'''
void put(FsVolume& volume, const char* path, const char* bytes) {
  auto file = volume.open(path, O_CREAT | O_EXCL | O_WRONLY);
  assert(file && file.write(bytes, strlen(bytes)) == strlen(bytes) && file.sync() && file.close());
}
void check(FsVolume& volume, const char* path, const char* expected) {
  char bytes[32]{};
  auto file = volume.open(path, O_RDONLY);
  assert(file && file.fileSize() == strlen(expected));
  assert(file.read(bytes, strlen(expected)) == static_cast<int>(strlen(expected)) && file.close());
  assert(std::string(bytes) == expected);
}
int main(int argc, char** argv) {
  assert(argc == 2);
  Disk disk; uint8_t scratch[512];
  if (std::string(argv[1]) == "fat") { FatFormatter format; assert(format.format(&disk, scratch)); }
  else { ExFatFormatter format; assert(format.format(&disk, scratch)); }
  FsVolume volume; assert(volume.begin(&disk)); SDCard.volume = &volume;
  assert(volume.mkdir("/books", true) && volume.mkdir("/.crosspoint", true));
  put(volume, "/.crosspoint/progress.bin", "reading state");
  put(volume, "/books/original.epub", "original");
  HalFile file;
  assert(openUploadFile("/books/Café book.epub", false, file));
  assert(file.write("new", 3) == 3 && file.sync() && file.close());
  HalStorage::MutationSnapshot snapshot;
  assert(Storage.beginMutationSnapshot(snapshot));
  const auto before = disk.writes;
  assert(!openUploadFile("/books/Café book.epub", false, file) && !file);
  assert(disk.writes == before);
  assert(Storage.mutationSnapshotUnchanged(snapshot));
  // Reproduce another writer winning between exists() and the upload open.
  assert(!volume.exists("/books/raced.epub"));
  put(volume, "/books/raced.epub", "competitor");
  const auto raced = disk.writes;
  assert(!openUploadFile("/books/RACED.EPUB", false, file) && !file && disk.writes == raced);
  assert(Storage.mutationSnapshotUnchanged(snapshot));
  assert(!openUploadFile("/books", false, file) && !file);
  assert(Storage.mutationSnapshotUnchanged(snapshot) && Storage.beginMutationSnapshot(snapshot));
  // Explicit overwrite still writes its staged path, leaving the original alone.
  assert(openUploadFile("/books/original.epub.part", true, file));
  assert(file.write("replacement", 11) == 11 && file.sync() && file.close());
  assert(openUploadFile("/books/original.epub.part", true, file));
  assert(file.write("short", 5) == 5 && file.sync() && file.close());
  // Genuine read failures retain the conservative checksum/inventory fence.
  disk.failReadAt = disk.reads;
  assert(!openUploadFile("/books/read-failure.epub", false, file));
  assert(disk.injectedRead && !Storage.beginMutationSnapshot(snapshot));
  SDCard.volume = nullptr; volume.end(); assert(volume.begin(&disk)); SDCard.volume = &volume;
  check(volume, "/books/Café book.epub", "new");
  check(volume, "/books/raced.epub", "competitor");
  check(volume, "/books/original.epub", "original");
  check(volume, "/books/original.epub.part", "short");
  check(volume, "/.crosspoint/progress.bin", "reading state");
  SDCard.volume = nullptr; volume.end();
}
'''

with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(code)
    (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long tick=1;return tick++;}\n')
    command = [os.environ.get('CXX', 'c++'), *hal.flags, '-U__ARM_FEATURE_UNALIGNED', *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(tmp / 'HostCompat.h'), '-I', str(ROOT / 'lib/hal'),
               '-I', str(hal.dependency / 'src'), str(tmp / 'test.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
    subprocess.run(command, check=True)
    for filesystem in ('fat', 'exfat'):
        subprocess.run([str(tmp / 'test'), filesystem], check=True)
print('Create-only upload collisions preserve files and reading state after FAT/exFAT remount')
