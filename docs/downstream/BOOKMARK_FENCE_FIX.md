# Bookmark directory false alarm

November 15, 2026, Eastern time. During test61 device checks, Chad confirmed
reading-position resume, opening the disposable EPUB, and saving its bookmark
and clipping. Back in File Transfer, checksum jobs returned HTTP 503 with
`state: unavailable` before processing bytes. No move request was sent. The test
book and its saved state remain at `/zDev/MoveCheck/Before/Move Check.epub`.

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
use that adapter's presence stub. The standard X4 Pro build passes; the combined
device build and independent review remain required before transfer.

## Device check

Install test62, reopen the same disposable book, turn one page, and add another
bookmark. Return to File Transfer. Checksum verification and the on-card move
should then proceed. Verify resume, bookmarks, clipping, and statistics after
the move. Keep the nine held books untouched until those checks pass. Test61's
test-book state and global statistics are backed up privately; no move journal
has been created by this test.
