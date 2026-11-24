# State-preserving book moves

Chad requested this on November 15, 2026: moving or renaming a book must retain
progress, statistics, clippings, native and imported CrossInk bookmarks, reader
preferences, protected-book sidecars, and current/recent references. On-card
moves must rename the EPUB rather than rewrite or transfer its contents.

The initial library job reported 1,408 reorganized books, 85 copied state files for
24 books, nine current/recent books held at their original paths, and all 1,474
catalog books present. After installing and testing test62, Chad authorized
moving the remaining nine. Those moves and their final verification are now
complete; results are recorded below.

## Implementation status

- [x] Inspect Wi-Fi, device rename, WebDAV, completion moves, state stores, and reader lifetime.
- [x] Define and test a bounded, checksummed plan and path mapping.
- [x] Implement sealed journal publication, rename/rewrite execution, and recovery.
- [x] Coordinate current/recent references in memory and on disk.
- [x] Route file-management entry points through the shared owner.
- [x] Test real FAT/exFAT interruption, conflict, state, bookmark, sidecar, and reference behavior.
- [x] Complete firmware checks, independent review, and a reviewable device build.
- [x] Fresh device backup and verified transfer into `/zDev/`.
- [x] Chad installs test61 and confirms his current book resumes correctly.
- [x] Verify a disposable book through move, rename, restart, resume, bookmarks, and clippings on test62.
- [x] Move the nine held books, verify their state, update sync receipts, and confirm all 1,474 catalog books remain present.

The transaction owner is connected to Wi-Fi move/rename, device rename, and
WebDAV book moves. Boot recovery runs after current/recent stores load and
before reader entry. A pending transaction blocks HTTP mutations before upload
streaming, reader entry, statistics edits, and Recents pruning. It has passed
independent review and device testing on test62. All nine held books have moved
to their planned library folders.

## Ownership and preservation

File Transfer replaces the reader and destroys its retained stack before serving
requests. The shared transaction must run on the main task, after live readers
and statistics leases are released, with render then storage locking. It must
reject destination/state collisions and unresolved conflicting transactions
before moving the book.

The plan covers the EPUB and `.key`/`.rights`, all five clipping-store variants,
native bookmark variants, both CrossInk bookmark-key families and recovery
variants, and native/legacy cache, statistics, and book-backup directories.
Keeping directory contents preserves progress backups and opaque state that a
fixed list would miss. CrossInk bookmark files embed the book path and require
checked, format-preserving rewriting. Unresolved or invalid artifacts must be
preserved and reported rather than guessed away.

Section caches contain absolute image paths; CSS caches can also refer to the
old cache root. They require controlled regeneration after a cache move.
Progress, preferences, statistics, and recovery records must not be cleared
with those generated files. Existing completion move/file and reference-write
primitives are the starting point; ordinary moves must not mark a book finished
or change reading totals.

The plan codec uses a maximum 1,023 bytes per book path and a 2,723-byte record,
allocated off the task stack by its eventual owner. It validates every entry's
role, path lengths, canonical names, transaction ID, exact length, and checksum.
It adds no setting, task, or persistent reader field. Runtime working-set and
firmware-size measurements remain pending until the executor is integrated.

Eight focused host checks pass normally and under AddressSanitizer and
UndefinedBehaviorSanitizer. They cover truncation, single-byte corruption,
invalid typed records with a recomputed checksum, maximum-length/Unicode paths,
both bookmark keys, protected sidecars, and complete state/backup roots. These
codec/path checks do not establish transaction recovery or device behavior.

## Verification checkpoint

The owner writes an immutable plan and separate seal before moving state. It
moves whole cache/statistics/backup roots, streams CrossInk bookmark path
rewrites while retaining originals, renames the book, invalidates only generated
section/CSS files, and updates references before archiving the journal. It adds
no task or setting. Working memory is one checked `Work` allocation plus bounded
cold-I/O path strings and existing reference-store JSON allocations. Runtime
internal/PSRAM heap and timing measurements remain device checks.

All 2,464 cumulative host checks pass (the full run plus one corrected host-adapter rerun). Normal moves, destination conflicts before
mutation, actual RAM/disk reference updates, both cover-cache roots, reader lease
exclusion, recovery gates, all 31 state roles, opaque directory contents,
progress backups, bookmark rewrites, protected sidecars and their temporary/
backup files, and retained original bookmarks are checked on FAT/exFAT images.
The 27-entry prototype's complete failure sweep covered 648 FAT and 765 exFAT
fail-stop write cuts under ASan/UBSan with the driver's bytewise endian helper;
the extended 31-entry cases pass in the cumulative suite. Initial sanitizer
attempts used SdFat's ARM unaligned-access shortcut; the host verification uses
`-U__ARM_FEATURE_UNALIGNED` to exercise the bytewise path also selected on Xtensa.
The original book's data sectors remain unchanged through every cut/retry.

Ambiguous, damaged, or unsealed journals stop further edits and retain artifacts.
This is not a general filesystem repair guarantee. Directory allocation tokens
identify observed roots, not volume-wide ownership. External card modifications
while a transaction is pending are unsupported. WebDAV moves books individually
and refuses destination overwrite; directory moves are rejected. HTTP status
and recovery contracts are in `docs/webserver-endpoints.md`.

The disposable-book move/rename check now passes on test62, including byte
comparisons and Chad's restart/resume and saved-item checks. Backup, transfer,
and device results are recorded below. The nine held books subsequently moved
and passed the final state and catalog checks.

## Review corrections

The first independent correctness/storage review found startup recovery running
before main-task ownership existed, normal resume clearing the saved book path
while recovery was pending, cover generation/deletion reaching transaction-owned
paths, WebSocket uploads bypassing the HTTP gate, and an unhandled CrossInk
`.bookmark-edit.tmp` sibling. These are corrected as one batch. Boot initializes
the activity manager before recovery and routes unresolved moves to File Transfer
before clearing any current-book reference. Cover generators, Library deletion,
Recents removal, deferred completion, and WebSocket writes stop while pending.
Both source/destination staging siblings in both bookmark-key families block
preflight before publication. Numbered retired bookmark snapshots are retained
at their existing historical paths.

Production boot-routing and WebSocket guards, Library deletion/index reopening,
cover-grid admission, pending Recents removal, deferred-move exclusion, and
staged-bookmark conflicts have focused regression coverage. Current 31-entry
fault sweeps cover every write position in a successful transaction: 669 FAT
cuts (114 resume, 555 preserve artifacts) and 777 exFAT cuts (104 resume, 673
preserve artifacts). Those sweeps, real RAM/disk reference tests, and lifecycle
checks pass ASan/UBSan. The host `Work` object is 3,568 bytes; device heap peaks
and largest blocks remain unmeasured. Both X4 Pro builds pass. Targeted second
correctness/storage review at `dcab13ba` found no remaining findings.

## Test61 Wi-Fi handoff

Completed November 15, 2026, Eastern time. The verified X4 Pro was running
`2.0.0-chad.3-test60-swipefix-x4pro`. The new image is
`/zDev/MOVE61.BIN`, expected version `2.0.0-chad.3-test61-bookmoves-x4pro`,
5,823,216 bytes, SHA-256
`24ca81fa3fdb54e3cada5633299a510456359c83f1b109fd01b5d89b772f4290`.
ESP32-S3 chip/header, checksum, validation hash, package checksums, reader-side
hash, and download readback all match. The source candidate is `dcab13ba`.
The SDK pin, all three patched SDK source hashes, and non-version build flags
match the preceding handoff. Both standard and combined X4 Pro profiles pass.

A fresh scoped Wi-Fi backup preserves 322 accessible state files totaling
449,861 bytes. Every file matches a second download before transfer and a fresh
download after cleanup. The temporary hidden-file setting was restored, and
original settings values match. Books, credentials, plugin configuration, and
rebuildable caches remain on the card; inaccessible hidden transaction leaves
are not covered. This is not a whole-card backup.

Test59 was archived privately on the Mac, verified, and removed from `/zDev/`.
The card retains `MOVE61.BIN`, installed test60's `SWIPE60.BIN`, and official
`crosspoint-2.0.0-x4pro.bin`; all three hashes were rechecked after cleanup.
No firmware was installed and no book was moved by the agent. Package and
handoff receipts remain privately under `.cache/device-test/`.

Chad installs from Settings → System → SD Card Firmware Update → zDev →
MOVE61.BIN. First reopen a current book and confirm its position. The subsequent
move check must use a disposable book with a clipping/bookmark and saved
position; verify those and its statistics after a Wi-Fi move/rename and restart.
The nine held books wait for that result.

## First device check

Chad reports the update installed and his current book resumed correctly. Wi-Fi
status confirms `2.0.0-chad.3-test61-bookmoves-x4pro`, book-move API version 1,
and no pending recovery. Reading statistics are enabled. An original four-chapter
test EPUB, `Move Check.epub`, is uploaded and hash/readback verified at
`/zDev/MoveCheck/Before/Move Check.epub`; its destination folder is
`/zDev/MoveCheck/After/`. It is disposable development material. The nine held
books have not been moved. Chad confirmed opening the test book and saving a
bookmark and clipping. Its progress, statistics, bookmark, and clipping files
are backed up. The first move preflight stopped before mutation because saving
the bookmark had latched the storage observer through an existing-directory
no-op. No move request was sent. Fixture and transfer evidence are private under
`.cache/device-test/move61-ondevice-qa/`.

## Test62 bookmark fix handoff

The bounded HAL fix at `401ebb6f` passes 2,465 cumulative checks, eight real
FAT/exFAT scenarios, ASan/UBSan, both X4 Pro builds, and independent correctness
and storage/embedded reviews. It declines an already-existing SDK path without
attempting a filesystem mutation. Real-fault and open-writer gates remain in
place. See [the bookmark fix record](BOOKMARK_FENCE_FIX.md) for the mechanism,
image hash, backup scope, transfer proof, and device check.

`/zDev/MOVE62.BIN` is verified by download readback. All 326 freshly backed-up
state files match after transfer; previous firmware images remain on the card.
Chad confirms installation and a new bookmark save. Wi-Fi status confirms
`2.0.0-chad.3-test62-bookmoves-x4pro`, and the test EPUB's checksum verifies after
the new bookmark save. This exercises the fix; restarting alone would only clear
the old process-local fault.

## Test62 device move and rename

Both `POST /move` and `POST /rename` return HTTP 200 for the disposable EPUB.
The folder move changes `/zDev/MoveCheck/Before/Move Check.epub` to
`/zDev/MoveCheck/After/Move Check.epub`; the filename rename then changes it to
`/zDev/MoveCheck/After/Move Check Renamed.epub`. Both operations rename existing
card entries. The EPUB's SHA-256 remains
`6a98787bd86cef8d9befd598f3e4659f8dc276060e24e2d26258a447e68f2372`.

Each operation has a fresh, twice-read backup of the scoped state before the
request. All 31 state roles are checked. Existing progress, statistics,
bookmarks, clipping, and retained cache files match byte for byte afterward.
Only the intended generated chapter/CSS cache invalidation is allowed. Old
state paths disappear; current-book, Recents, and cover-cache paths update.
Unrelated Recents entries, global/daily statistics, and settings match their
pre-operation contents. Both operations report no pending recovery. Receipts
remain private under `.cache/device-test/move62-ondevice-qa/` and
`.cache/device-test/rename62-ondevice-qa/`.

After restarting the reader, Chad confirms that opening Move Check from Recents
resumes correctly, a bookmark opens the expected passage, and the saved clipping
has the correct text and opens the correct passage. This completes the
disposable-book move/rename smoke test. Statistics preservation is verified by
file comparison. At this point, the nine real books were still held; their
subsequent authorized moves are recorded below.

## Nine held books completed

Chad authorized the remaining moves after the disposable-book checks. The
saved library plan matches the current catalog and sync receipts. A fresh
Wi-Fi backup contains 334 accessible persistent-state files totaling 459,416
bytes, each verified by two downloads. Hidden-file visibility was restored.
Preflight found all nine source EPUBs and no destination or state conflicts.

All nine books moved through test62's state-preserving `POST /move` API:

- *The End of Everything* moved to `/Fiction/Science Fiction/Science Fiction and Fantasy/`.
- *Character*, *Dialogue*, *Story*, and *Action* moved to `/Non-fiction/Writing and Storytelling/`.
- *If Anyone Builds It, Everyone Dies*, *Supremacy*, *Co-Existence*, and *The Shortest History of AI* moved to `/Non-fiction/Technology and AI/`.

The existing EPUB entries were renamed on the card. Each of the nine EPUBs
matches its catalog SHA-256 before and after its move. Per-book snapshots check
all 31 state roles and compare every existing persistent file byte for byte.
Retained derived images/HTML are checked by presence and size; generated chapter
and CSS caches are invalidated as designed. Current/recent references and
cover-cache paths update, while unrelated references, global/daily statistics,
and settings remain unchanged during each operation.

The final comparison verifies 331 backed-up files at their original or mapped
paths. Current/recent JSON is checked separately against the baseline plus all
nine intended path mappings; the library dirty marker is the expected index
invalidation. Only the nine relevant sync receipts change, and all nine now
record verified hashes at their new paths. A fresh inventory finds all 1,474
catalog books at their recorded paths and sizes. No pending firmware recovery
is reported, and the temporary library migration marker is removed.

Operational scripts, scoped backups, per-book proofs, the final inventory, and
the complete journal remain private under
`.cache/device-test/held-books62-20261116T044058Z/`. A completion receipt is also
saved beside the original library plans as
`reader-held-moves-test62-complete.json`. The test62 restart/resume and saved-item
smoke test used the disposable book; these nine real-book checks are automated
checksum, state, reference, and inventory comparisons.

## Deleting an EPUB on a nearly full card

Before staging an EPUB deletion for library sync, the reader checks for room to
write its journal, mark the library index dirty, create any missing folders,
move the EPUB, and archive the receipt. The estimate allows for directory
growth. If the free clusters fall below that bound, the original book stays
in place. The Library and File Browser show a message asking the reader to
free space. Wi-Fi File Manager returns HTTP 507 and lists failed items. Other items
in the same batch can succeed. Retry only the listed failures. If the
allocation map cannot be read, the deletion stops. The File Manager shows a
deletion failure, and the reader logs it. Ordinary moves and
renames continue to use their existing journal and recovery behavior.

The allocation check uses the mounted volume under the storage lock. On
newly formatted 1–128 GiB FAT and exFAT images, it read one or two sectors.
A nearly full or fragmented card can require scanning the entire allocation
map. The duration on the X4 Pro is unmeasured.

Across 164 simulated attempts on FAT and exFAT with zero through 40 free
clusters, 96 deletions completed on the first attempt. The other 68 wrote
nothing before reporting insufficient space. All 68 completed after freeing
space. After remount, every held book matched its original bytes and receipt.
The fixtures cover both missing and existing destination folders.

## Names that fit a deletion receipt

For individual EPUB deletions outside `/zDev/`, a long English or Unicode
name can be staged if the complete holding path fits and the file is smaller
than 4 GiB. Larger files, including exactly 4 GiB, stay on the card with
a size message. The checksum path permits up to 32 components. Hidden
components and short-name aliases cannot identify a synced book. If adding
the `.held` suffix exceeds the FAT filename limit of 255 UTF-16 code units,
or the full path exceeds the journal limit, the reader
keeps the original and asks for a shorter name or path. Wi-Fi File Manager
lists the affected files and returns HTTP 422. A mixed batch with a low-space
failure returns 507. A zero-byte EPUB at a valid source path uses local
deletion, as do files under `/zDev/` outside the protected receipt queue.

The Wi-Fi file list can return names up to 765 UTF-8 bytes, including the
holding folders. An interrupted directory read ends the response before the
closing bracket so a client can retry the incomplete listing. A confirmed
missing folder returns an empty list. An unreadable folder returns HTTP 503.
The on-device File Browser checks each name before offering actions or
constructing a child path during folder deletion.
