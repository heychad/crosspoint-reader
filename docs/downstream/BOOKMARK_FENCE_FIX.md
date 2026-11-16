# Bookmark directory false alarm

November 15, 2026, Eastern time. During test61 device checks, Chad confirmed
reading-position resume, opening the disposable EPUB, and saving its bookmark
and clipping. Back in File Transfer, checksum jobs returned HTTP 503 with
`state: unavailable` before processing bytes. No move request was sent at that
point. The test book and its saved state stayed at
`/zDev/MoveCheck/Before/Move Check.epub` until the test62 checks below.

## Cause and correction

`BookmarkUtil::getBookmarksDir()` returns `/.crosspoint/bookmarks/`, including
the trailing slash. `BookmarkFile::save()` passes that spelling to `mkdir`.
The HAL's existing-path guard also required the strict recovery lookup to
return Present. That lookup intentionally rejects trailing slashes. The guard
therefore fell through to an unnecessary directory-creation attempt. SdFat
returned false because the directory already existed, and the HAL permanently
invalidated its process-local storage observer. Checksum and move admission
then stopped, even though saving the bookmark succeeded.

The HAL now returns false immediately when the SDK reports the path already
exists. This is a no-op, including existing files and SDK-supported directory
spellings. It grants no write or recovery permission. Strict recovery lookups,
new directory creation, open-writer exclusion, and real I/O-failure handling
remain unchanged. No new allocation, task, setting, SDK patch, or persistence
format is introduced. The candidate version is
`2.0.0-chad.3-test62-bookmoves-x4pro`.

## Verification

The new regression executes production HAL directory creation, lookup, file
ownership, and mutation-observer methods on patched SdFat FAT/exFAT images.
Before the fix, the bookmark-style trailing slash reproduces the observer
failure. After the fix, existing canonical/trailing/multiple-slash directory
paths and existing files cause no writes, no backend mkdir call, and no epoch
change. New directories still invalidate old snapshots and admit a fresh one.
An open writer still blocks snapshots; injected write failures, driver faults,
and failed creation below a regular file retain the failure gate. All eight
filesystem/scenario combinations pass normally and under ASan/UBSan using the
bytewise endian path selected on Xtensa.

All 2,465 cumulative checks pass across the full run and a corrected lifecycle
adapter rerun. That adapter had reported both an existing SDK path and a missing
checked path while asking mkdir to create a new directory; it now marks that
test path missing before creation. The new real-filesystem regression does not
use that adapter's presence stub. Both `x4pro` and `x4pro-chad-test` builds pass.
Independent correctness and storage/embedded reviews of `401ebb6f` against
`68e13a89` found no remaining findings. The SDK pin and three existing source
patches are unchanged. The combined profile uses 113,100 bytes of RAM and
5,817,950 bytes of flash; these are linker sizes, not measured runtime peaks.

## Device check

Test62 is transferred to `/zDev/MOVE62.BIN`, 5,823,216 bytes, SHA-256
`ff248d11269723b25b3c607b62c2b6d0276f517906fc792ffede6434c3b57120`.
The ESP32-S3 image checksum and validation hash pass. A download readback matches
the packaged image. That transfer used download readback while test61's checksum
API was unavailable. Test62 subsequently passes the checksum and device checks
below.

A fresh scoped Wi-Fi backup contains 326 accessible state files totaling
451,057 bytes. Each file matches two pre-transfer downloads and a post-transfer
download. Temporary hidden-file visibility is restored, and settings values
match. The scope excludes books, credentials, plugin configuration, generated
caches, and inaccessible hidden transaction leaves. `MOVE61.BIN`, `SWIPE60.BIN`,
and the official `crosspoint-2.0.0-x4pro.bin` remain in `/zDev/`; each matches its
previous image hash by download readback. No old images were deleted.
Package, backup, and transfer receipts remain private under
`.cache/device-test/`.

Chad confirms test62 is installed and a new bookmark saves normally. Wi-Fi status
confirms `2.0.0-chad.3-test62-bookmoves-x4pro`. Checksum jobs succeed after that
bookmark save. Both an on-card folder move and filename rename return HTTP 200
and preserve the EPUB, progress, statistics, native bookmarks, and clipping
bytes. Current-book, Recents, and cover-cache paths update; unrelated Recents,
global statistics, daily statistics, and settings remain unchanged. No pending
recovery is reported after either operation.

The disposable book is now
`/zDev/MoveCheck/After/Move Check Renamed.epub`. After restarting the reader,
Chad confirms correct resume from Recents, bookmark navigation, and clipping
text and passage navigation. The move/rename device smoke test passes. The
nine held books remain untouched. Fresh per-operation backups and receipts
are private under `.cache/device-test/move62-ondevice-qa/` and
`.cache/device-test/rename62-ondevice-qa/`. See
[the move record](STATEFUL_BOOK_MOVES.md#test62-device-move-and-rename).
