# State-preserving book moves

Chad requested this on November 15, 2026: moving or renaming a book must retain
progress, statistics, clippings, native and imported CrossInk bookmarks, reader
preferences, protected-book sidecars, and current/recent references. On-card
moves must rename the EPUB rather than rewrite or transfer its contents.

The library thread reports 1,408 reorganized books, 85 copied state files for
24 books, nine current/recent books held at their original paths, and all 1,474
catalog books present. Its local plans and receipts are input evidence, not
authorization to move the nine or install firmware. Those actions require a
fresh backup and Chad's device-update approval.

## Implementation status

- [x] Inspect Wi-Fi, device rename, WebDAV, completion moves, state stores, and reader lifetime.
- [x] Define and test a bounded, checksummed plan and path mapping.
- [x] Implement sealed journal publication, rename/rewrite execution, and recovery.
- [x] Coordinate current/recent references in memory and on disk.
- [x] Route file-management entry points through the shared owner.
- [ ] Test real FAT/exFAT interruption, conflict, state, bookmark, sidecar, and reference behavior.
- [ ] Complete firmware checks, independent review, and a reviewable device build.
- [ ] Fresh device backup, approved installation, and disposable-book verification.

The transaction owner is connected to Wi-Fi move/rename, device rename, and
WebDAV book moves. Boot recovery runs after current/recent stores load and
before reader entry. A pending transaction blocks HTTP mutations before upload
streaming, reader entry, statistics edits, and Recents pruning. It has not been
reviewed or installed yet; the nine held books must remain at their paths.

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

The next hardware check must use a disposable copy first. A fresh state backup,
validated image transfer into `/zDev/`, Chad's installation, and reader-side
acceptance are still required. The nine held books have not been moved.

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
review is required before transferring this candidate.
