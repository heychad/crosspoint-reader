# Library deletion sync handoff

Recorded November 22, 2026, from the xteink chat. Firmware work belongs in the
crosspoint-fork chat. The xteink chat owns reader/library operations, EPUB
repairs, transfers, and library organization.

**Keep test62 installed.** The November 23 pre-install review found defects
in test63's deletion records and interruption handling. The X4 Pro at
`192.168.86.176` reported test62. Both `/zDev/DEL63.BIN` and `/zDev/MOVE62.BIN`
matched their packaged SHA-256 hashes. A read-only backup verified 483 accessible
state files totaling 128,520 bytes. Its receipt is private under
`.cache/device-test/test63-precheck-20261123T160419Z/`.

## Behavior

Library commit `6fcac08` implements recoverable deletion sync. A compatible
reader must report an intentional individual EPUB deletion. The library
verifies the event and bytes, moves the Drive copy out of the active library
before syncing books, and retains it for recovery for 60 days. A missing path
alone never triggers deletion.

The candidate's explicit device deletion clears that book's reading cache and
clippings. Restore brings back the EPUB. This behavior must be clear during
the disposable-book check. Preserve test62's state-preserving moves and
checksum behavior; moving a book must not produce an intentional-deletion event.

## Candidate and package

- Worktree: `/Users/heychad/code/crosspoint-reader-library-deletions`.
- Branch: `codex/xteink-library-deletions`, based on `8a1b6d84`.
- Source changes remain uncommitted in that isolated worktree.
- SDK pin: `5e94fe7480f019c89fbf691aef87523eb1b46704`.
- Build target: `x4pro-chad-test`; expected version `2.0.0-chad.3-test63-x4pro`.
- Package: `/Users/heychad/Library/Application Support/Xteink/Firmware/2026-11-20-library-deletions/`.
- Candidate: `DEL63.BIN`, 5,825,984 bytes, SHA-256
  `563969a24634a6a457b423ad88938ed71de67ffde3fbb9ce8a6accc801db0327`.
- Rollback: `MOVE62.BIN`, 5,823,216 bytes, SHA-256
  `ff248d11269723b25b3c607b62c2b6d0276f517906fc792ffede6434c3b57120`.
- Packaged source: `SOURCE.patch`, SHA-256
  `9540444a4579151ab0e63463f3ea5f4f135ca1d5fb9965bc20f7cd8556622696`.

The handoff and package README record a passing X4 Pro build, image validation,
2,465 host firmware checks, and 350 library tests. On November 22, this chat
rechecked all three package checksums and confirmed that `SOURCE.patch` exactly
matches the current worktree's full-index binary diff, excluding the SDK
submodule. The SDK pin also matches the package record. Tests were not rerun
for this documentation handoff. The candidate worktree and its SDK changes
remain untouched.

## Pre-install review

Independent correctness and storage reviews covered the packaged patch against
`8a1b6d84`. They found the following blockers in the deletion workflow:

- Ordinary file operations can change queued deletion records and held EPUBs.
  Reserve mutation of this queue for the deletion workflow and verify the
  receipt against the exact book and source path before releasing its bytes.
- An interrupted rename can leave an incomplete deletion without enough durable
  information for recovery. Reuse the checked move recovery path or provide an
  equivalent transaction that can reconcile the card safely after restart.
- Cleanup can report a committed deletion as failed and can disable checksum
  and move operations until restart. Keep the deletion result separate from
  cleanup, and validate directory removal before attempting it.

The current host test checks the Library callback with a stub. The next
candidate needs production-path FAT/exFAT coverage for queue ownership,
staging, interrupted writes, and cleanup. It also needs to retain test62's
move and checksum behavior. Device installation and release remain pending.

## Pending acceptance

- [ ] Fix and verify queue ownership, recovery, and cleanup in a replacement candidate.
- [ ] Verify the replacement with Chad on the X4 Pro.
- [ ] Delete a disposable synced EPUB, run Check reader, and confirm Recently deleted shows a 60-day expiry.
- [ ] Restore, sync, and open that disposable EPUB again.
- [ ] Move a different book with saved progress; confirm resume and that the move does not appear in Recently deleted.
- [x] Library-side recovery workflow exists at `6fcac08`.
- [x] Firmware candidate built and packaged; checks recorded in its README.
- [x] Local package hashes and packaged-source/worktree match rechecked.
- [x] Candidate copied to `/zDev/DEL63.BIN` and checksum-verified, per the November 22 handoff.
- [x] November 23 pre-install review, card checksum recheck, and scoped state backup.

Keep release readiness separate from compilation, package verification, and
copying the candidate to the card. Follow the repository's firmware-handoff
rules when Chad resumes device or release work.
