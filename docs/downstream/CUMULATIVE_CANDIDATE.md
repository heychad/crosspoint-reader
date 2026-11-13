# Combined X4 Pro candidate

November 13, 2026. This is the cumulative review record, not installation approval. Round-one
review is complete; corrections are in progress.

## Requirement and frozen inputs

Implement Chad's 28 selected Now feature cores from
[the parity table](FEATURE_PARITY.md), preserving CrossPoint 2.0.0 reading,
plugin/protected-book interfaces, and existing reading state. Later features
remain deferred; Nearby is skipped. Device performance tuning follows feature
integration, with measurements required before claiming improvement.

- Review source: `f3b28c79a4b75236404c3965dab3cefc4fcbfcb1`.
- Source tree: `0e7f79cc951f152fdda9864dce65f0742013a0c5`.
- Working branch: `codex/x4pro-release-baseline-20261111`.
- Comparison base: CrossPoint 2.0.0, `42d58daaabcf469a2e5e1ba871714fb9dcf51d29`.
- CrossInk references: stable v1.6.1 and tagged v2.0.0-rc2
  (`959e3d1575cf1cf49475c4c4546e49adadf4d477`). These are feature references,
  not an SDK or whole-tree merge.
- SDK pin: `5e94fe7480f019c89fbf691aef87523eb1b46704`.
- Private client: second-brain branch `codex/x4pro-library-sync-20261113`,
  `9f4db2f4c0957486809c482318243b0f22e6560d`. Its code/tests match canonical
  checkout `6c94b59`; README records the publication branch's smaller test scope.

The baseline-to-candidate diff contains 459 production/build/script paths, plus
tests and documentation. Use `git diff --name-only` with the pinned revisions
for the exact list. [Release reconciliation](RELEASE_BASELINE.md) records the
excluded development series. Earlier focused reviews remain evidence for their
unchanged inputs; they do not replace review of the combined contracts.

The SDK has three expected local modifications represented by checked patches:
the SDMMC health monitor, mounted filesystem type, and verified TLS setup.
`patch_freeink_sdmmc.py`, `patch_freeink_volume.py`, and `patch_freeink_tls.py`
verify/apply them. The cached SDK link repair must run before library discovery.
Other SDK edits or a changed pin are not part of this candidate.

## Intended configuration

The tested combined environment is `x4pro-personal-updates-wip`, using the
X4 Pro/S3/PSRAM base and these twelve flags set to 1:

```text
CROSSPOINT_EXPERIMENTAL_COMPLETION
CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT
CROSSPOINT_EXPERIMENTAL_STATS_RESTORE
CROSSPOINT_EXPERIMENTAL_DAILY_STATS
CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY
CROSSPOINT_EXPERIMENTAL_CAROUSEL
CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE
CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME
CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION
CROSSPOINT_EXPERIMENTAL_STATS_BACKUP
CROSSPOINT_EXPERIMENTAL_CHAPTER_PREFETCH
CROSSPOINT_EXPERIMENTAL_PERSONAL_UPDATES
```

The exact local profile is reproduced in [development process](DEVELOPMENT_PROCESS.md).
Inventory, stage, and publication prototype flags are absent. D1 uses the
existing selected-book app, ordinary exclusive upload, and checksum/download
verification. The default and release profiles still leave WIP options disabled.
The current build string remains test58; packaging needs a distinct test version
and a checked-in reproducible candidate profile before handoff.

## Recorded checks

| Check | Evidence and limit |
| --- | --- |
| Latest default X4 Pro build | 71.77 s; RAM 113068, flash 5696342. Log `/tmp/x4-create-upload-default.log`. |
| Latest twelve-flag X4 Pro build | 76.96 s; RAM 113100, flash 5803398. Log `/tmp/x4-create-upload-enabled.log`. These are build footprints, not runtime peaks. |
| Update path | 43 focused checks and both sanitizers pass. Real host pinned-root API/redirect/CDN and rejection checks pass; firmware TLS cryptography and runtime resources remain untested. See [update contract](PERSONAL_UPDATES.md). |
| Sync client | Eight full-queue HTTP cases; all 303 canonical tests and 68 concepts pass. Code-only publication passes 73 tests and five base concepts. These use a host reader fixture. |
| Exclusive upload | Nine focused firmware checks; real FAT/exFAT remount and ASan pass. UBSan reports retained SdFat alignment/null-pointer findings; it is not a clean UBSan pass. See [sync contract](FILE_CHECKSUM_API.md). |
| Cumulative host suite | Frozen source `f3b28c79` passed all 2431 checks in 447.16 s. Logs: `/tmp/x4-candidate-host-build.log` and `/tmp/x4-candidate-host-tests.log`. Later corrections need their own checks. |
| Physical reader | Clipfix2 is the last accepted baseline. No later source in this packet has been installed or physically validated. |

## Round-one findings and corrections

All four independent axes completed read-only review of the pinned source.
They inspected central contracts and selected callers; this was not exhaustive
line-by-line inspection of all 459 production/build/script paths. No reviewer
ran tests, builds, writes, or device operations. The findings below are not
resolved by the passing pre-correction suite.

| ID | Finding | Current status |
| --- | --- | --- |
| R1, P1 | Per-book JSON retries can delete a `.tmp`/`.bak` alias sharing the live primary's chain. | Corrected under one storage lock with checked lookups, disjoint-chain checks, exclusive staging, sync/close/readback, and checked rename results. Real FAT/exFAT alias regressions fail before the fix and pass afterward. Awaiting targeted cumulative review. |
| R2, P1 | Completion accepts deep existing book paths, but its later source-reference audit applies staged-publication's 16-component limit after moving files. | Corrected: source audits use the existing reference validator; publication retains its narrower policy. Production guard, move, retry, retirement, and remount checks pass for a 17-component source on FAT/exFAT. |
| R3, P2 | Routine application-state saves consume permanent history slots; exhaustion blocks later saves and causes increasing scan work. | Corrected: clean routine saves retire only their checked independent backup. Existing history and uncertain recovery artifacts remain preserved; move history keeps its separate policy. |
| R4, P2 | Rotated Quick Lock activation and unlock resolve asymmetric side holds differently. | Corrected with the shared orientation mapping; regression failed before the fix and passes afterward across all four orientations. |
| R5, P2 | Completion shortcuts retain the old `/read` restriction despite deferred automation. | Corrected to match the menu's feature gate; disabled, legacy, and deferred profiles are tested. |
| R6, P2 | Backup/rollback instructions omit root-level completion journals, deferred intents, and history. | Corrected: backup and restore instructions cover a coherent snapshot of transaction directories, books at source/destination paths, sidecars, and reading state. |
| R7, P2 | Dashboard month abbreviations bypass translation. | Corrected: twelve short-month keys use `tr()` with English fallback. Native checks cover all months, invalid dates, and translated UTF-8 text. |
| R8, P3 | Minimal Home QA still describes the older screen without statistics. | Corrected: checks now cover the reading-time footer, progress bar/percentage, and missing values. |

R4/R5 add no persisted field, task, or allocation. The new host settings fields
exist only in a test stub. Twenty-one focused checks pass in 0.55 seconds; twenty
Quick Lock tests pass both sanitizers in 0.35 seconds, and all three completion
profiles pass both sanitizers. The existing oriented-side harness passes ASan.
The tests exercise production control code, with hardware/activity adapters.
The default X4 Pro build passes in 68.52 seconds (RAM 113068, flash 5696414);
the twelve-flag build passes in 70.20 seconds (RAM 113100, flash 5803354).
Logs use `/tmp/x4-review-input-*.log` and `/tmp/x4-review-completion-*.log`.
These are scoped correction checks, not a second cumulative review or a
post-correction full-suite pass.

R1 keeps the existing per-book format and primary/backup/temp load priority.
It checks all three files before any cleanup, including an unchanged save.
Independent interrupted saves can recover or reuse staging; shared or unreadable
chains remain untouched and the save fails. This is not filesystem repair or a
proof against aliases outside these three paths. Preserve ambiguous files for
inspection rather than deleting one to force a retry.

The per-book production suite passes 97 checks, including failed lookup, read,
lock, allocation, sync, close, short-write, and rename recovery. It also passes
ASan/UBSan. The production writer and selected production HAL methods pass ASan
on real FAT/exFAT images: eight failed-rename aliases, 76 interrupted writes,
and 187 read-fault positions. A changed preference retry, later allocation,
remount, and unchanged reading-position bytes are checked. A successful return
requires the requested bytes after remount; a transient low-level read error
may recover inside SdFat. The same alias regression fails on the prior writer.
The default X4 Pro build passes in 105.64 seconds (RAM 113068, flash 5697014);
the twelve-flag build passes in 106.14 seconds (RAM 113100, flash 5803854).
Logs use `/tmp/x4-perbook-*.log`. These checks do not replace the final full suite
or targeted cumulative review.

Serialization still owns at most 4097 bytes for the 4096-byte per-book limit.
The two existing path strings live only for the save. Existing HAL chain checks
temporarily allocate at most two 64-cluster lists (512 bytes), plus their file
handles and bounded path lookup buffers. No cache, task, or retained allocation
is added. For device verification, change a disposable book's layout twice,
close and reopen it, and confirm the layout and reading position survive sleep.

R2's native regression and real production-store regression both fail before
the correction. The real test runs both Recents policies on FAT/exFAT with the
production source-reference guard, state/Recents stores, move transaction,
clipping/bookmark stores, and reader progress loader. It checks interrupted
moves and retries, retirement receipts, companion files, remounts, and acquiring
a normal statistics session after retirement. Publication's 16-component limit
is unchanged. Logs use `/tmp/x4-deep-*.log`.

The follow-up source preflight shares that existing validator with completion
entry and move execution. Names resembling 8.3 aliases (for example,
`Part~1.epub`) previously could start a journal and move before the reference
audit rejected them. New direct/deferred move requests now return Invalid
before changing statistics or starting a journal. Ordinary finished marking
without a move still works. An older prepared unsupported plan returns Conflict
before moving files or marking the index dirty; its book, journal, and plan stay
available for inspection. This does not repair older in-flight transactions.
The extraction adds no allocation, task, or persisted field.

The new request regression fails before the fix. Sixty-four focused native
checks pass under ASan/UBSan in 2.91 seconds, including ordinary completion,
unsupported requests, older prepared plans, and reference audits. The real
deep-source move test passes both sanitizers on FAT/exFAT. The default X4 Pro
build passes in 72.02 seconds (RAM 113068, flash 5698134); the twelve-flag build
passes in 71.96 seconds (RAM 113100, flash 5804982). Logs use
`/tmp/x4-source-preflight-*.log`. Overlapping initial host builds were discarded;
the final host build/check ran alone from the corrected files.

R3 changes only retirement after a verified clean routine state save. The
primary has been synced, closed, reopened, compared with the requested bytes,
and checked independent from the old backup before that backup is removed.
Failed or ambiguous transactions still use explicit recovery and preserve their
artifacts. Old `.sleep-history-*` files are left untouched. Native tests cover
128 repeated changes and a full 10,000-slot history namespace. ASan real
FAT/exFAT tests cover 128 changes per filesystem and 88 interrupted writes with
changed-value retry, later allocation, remount, and preserved history/progress.
No allocation, state format, or background task is added. Logs use
`/tmp/x4-state-history-*.log`. Check repeated sleep/wake and saved position on
the next reviewed device candidate.

R6/R8 were checked against the transaction paths and current Minimal Home
rendering. R7 adds translation keys without changing saved dates, date formats,
or persistent settings. Missing translations use English rather than slicing
full month names. All corrections still need the targeted second cumulative
review; this table does not claim release or device acceptance.

The R2/R3/R7 correction set passes 19 focused native ASan/UBSan checks in
0.37 seconds, both native state-store profiles under both sanitizers, and six
translation checks. The initial firmware compile caught a dynamic-key `tr()`
macro mismatch; literal-key month dispatch and a matching host macro corrected
it. The final default X4 Pro build passes in 86.33 seconds (RAM 113068, flash
5698078), and the twelve-flag build passes in 103.85 seconds (RAM 113100,
flash 5804870). Logs use `/tmp/x4-review-storage-*.log` and
`/tmp/x4-review-i18n-*.log`. R1's real-filesystem retry checks also pass with
explicit requested-byte verification after successful changed-value retries.
No post-correction full-suite or second-review pass is claimed.

The retained vendor reports now have pinned fixes. Cached FAT directory reads
no longer advance their null copy destination; exFAT name hashing uses unsigned
intermediates with the same 16-bit results. Both defects reproduce under
ASan/UBSan before the patches. Afterward, 40 real FAT long-name files survive
lookup/iteration and remount, and both hash overloads match an independent
rotate/add calculation for all 65,536 seeds with ASCII and Unicode inputs.
All 32 upstream/cache migration paths pass. The SDK revision and file formats
are unchanged. Mac tests select X4 Pro's bytewise little-endian helper path;
they do not disable any sanitizer category. Logs use `/tmp/x4-sdfat-*.log`.

Create-only uploads now recheck the path and perform their exclusive open under
one HAL storage lock. A known collision avoids the failed writable open that
would poison checksum/inventory retries. Actual read failures still invalidate
the fence; no health flag is reset. The real FAT/exFAT regression fails before
the change and passes under both sanitizers afterward, preserving competitor
files, explicit-overwrite staging, and reading state after remount. Existing
HAL writer-lifetime checks also pass. The callback borrows the caller's path and
file references on the stack. Its checked lookup's temporary path copy is
released before returning; allocation failure stops before file creation.
Logs use `/tmp/x4-upload-fence-*.log`.

The per-book and routine-state real-filesystem failure suites now also pass
both sanitizers with the vendor fixes. Slow-card synchronous hashes and runtime
headroom still need measurement; no measured watchdog failure is established.
The default X4 Pro build passes in 74.44 seconds (RAM 113068, flash 5698134);
the twelve-flag build passes in 77.03 seconds (RAM 113100, flash 5804922).
FAT, exFAT, and wrapper lookup suites also pass both sanitizers on the target's
bytewise helper path. The full cumulative suite and second review remain open.

## Review and remaining gates

This candidate combines persistent writes/migrations, resource ownership,
SDK changes, input, and UI. The firmware-handoff skill therefore requires four
independent review axes: correctness, architecture, embedded, and i18n/docs.
Reviewers read the pinned code, history, and recorded checks only. The main agent
owns fixes and verification; source stays frozen during review.

- [x] Complete cumulative host checks for the round-one frozen source and record the terminal result.
- [ ] Revalidate the final correction set before device handoff.
- [ ] Complete all four review axes and resolve material findings.
- [x] Reproduce and correct retained vendor UBSan findings and benign upload-collision retry fencing, with target-path sanitizer checks.
- [ ] Stamp a unique test version and record a reproducible candidate profile.
- [ ] Measure runtime internal/PSRAM free bytes, largest blocks, and stack.
- [ ] Package source, exact configuration, hashes, rollback, and the short QA plan.
- [ ] Reidentify the reader, back up its state, and verify the backup's coverage.
- [ ] Prepare verified files under `/zDev/`; Chad performs installation.
- [ ] Confirm reading state, inputs, clipping, and representative new features.

The five-minute smoke test starts with a known book's resume position, clipping,
page turns/Quick Lock, one representative feature, and sleep/resume. Longer
backup/restore, completion-move, TLS-failure, and throughput checks use disposable
books and separate sessions. [The broader QA list](NEXT_DEVICE_QA.md) remains a
menu of later checks, not one five-minute assignment.

No device polling, flash, release publication, supported-branch merge, or PR is
authorized by this review record. Standing authorization covers checked WIP
checkpoints and pushes; device installation remains the reviewed human step.
