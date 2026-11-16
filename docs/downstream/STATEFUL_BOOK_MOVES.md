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
- [ ] Implement sealed journal publication, rename/rewrite execution, and recovery.
- [ ] Coordinate current/recent references in memory and on disk.
- [ ] Route file-management entry points through the shared owner.
- [ ] Test real FAT/exFAT interruption, conflict, state, bookmark, sidecar, and reference behavior.
- [ ] Complete firmware checks, independent review, and a reviewable device build.
- [ ] Fresh device backup, approved installation, and disposable-book verification.

The new plan is not connected to production file operations yet. Current Wi-Fi
move/rename still clears caches and is unsuitable for the nine held books.

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

The plan codec uses a maximum 1,023 bytes per book path and a 2,639-byte record,
allocated off the task stack by its eventual owner. It validates every entry's
role, path lengths, canonical names, transaction ID, exact length, and checksum.
It adds no setting, task, or persistent reader field. Runtime working-set and
firmware-size measurements remain pending until the executor is integrated.

Eight focused host checks pass normally and under AddressSanitizer and
UndefinedBehaviorSanitizer. They cover truncation, single-byte corruption,
invalid typed records with a recomputed checksum, maximum-length/Unicode paths,
both bookmark keys, protected sidecars, and complete state/backup roots. These
codec/path checks do not establish transaction recovery or device behavior.
