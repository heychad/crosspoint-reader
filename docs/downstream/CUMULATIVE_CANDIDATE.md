# Combined X4 Pro candidate

November 13, 2026. This is the cumulative review record, not installation approval.
Round-one corrections and retained storage fixes have checked checkpoints.
The second review is complete. Its host adapter, provenance, and unsupported-move
notification follow-ups are corrected and checked. Test59 is verified on the SD
card after a scoped Wi-Fi state backup; runtime/device acceptance remains pending.
The [handoff record](TEST59_HANDOFF.md) identifies the final image and checksums.

The subsequent [test60 swipe patch](SWIPE_LINK_RETURN_FIX.md) corrects a reported
input regression without changing persistence or the SDK. Its separate source,
checks, review, and package are recorded there; physical confirmation is pending.

## Post-test70 review

The X4 Pro comparison starts at test70 source `8959351d` and includes the
follow-up batch through `c06f2d12`. It keeps CrossPoint 2.0.0 and the pinned
FreeInk SDK `5e94fe7480f019c89fbf691aef87523eb1b46704` with the existing
patches. The standard and personal test profiles are the intended build
checks. The personal profile enables the same twelve selected flags as
test70.

Independent correctness, architecture, embedded, and documentation reviews
found four firmware regressions. They are covered by the corrective tests.

- Validate the first XTC page before using it to set the depth of later
  pages. A damaged first page with the opposite magic no longer blocks a
  valid second page. The correction checks both buffered and streaming reads.
- Keep a fallback line for borderless dividers painted with a recognized
  background color. Transparent and white colors keep the rule invisible
  when its borders and background image are absent.
- Bound invisible divider spacing and keep the first text on an empty page
  within view.
- Use the release handler for a remapped Up press while a page turn is pending.

Targeted second review cleared the four findings. A subsequent focused
correction handles decimal RGB inputs as complete values. The direct
[test70 checklist](NEXT_DEVICE_QA.md) now identifies the staged test65 rollback,
the version-3 completion journal requirement, and the disposable Drive
restore check. Physical review starts with test70.

The corrected source passed all 2,563 enabled host checks in 130.02 seconds.
The 31 CSS parser and 10 XTC parser checks passed AddressSanitizer and
UndefinedBehaviorSanitizer. The remapped-input fixture passed both sanitizers.
The standard X4 Pro built in 76.39 seconds and the personal profile in
69.20 seconds. The 5,860,736-byte personal WIP image passed the board,
version, slot size, checksum, and validation hash checks. Its SHA-256 is
`24d6f0ce0cdc2a60d32f9ff56e0d5f21e08a6aadeed6c86c36b2fd33129a6e11`.
The earlier simulated stable upgrade is recorded separately with its
[preview revision and image hash](PERSONAL_UPDATES.md#post-test70-integration-check).

### Local test71 package

The follow-up package `2026-11-23-test71-review-fixes` uses firmware source
`f7257e093415139099acacda342a50c8463b0a4f`. The version-only personal build
took 75.05 seconds and reports `2.0.0-chad.3-test71-review-fixes-x4pro`.
The 5,860,848-byte `READER71.BIN` passed the board, version, slot size,
checksum, and validation hash checks. Its SHA-256 is
`487ab6b2fd68e1ade483f79e4fca76ebf0c8e1271b2d67399607d1909db49862`.
The local manifest also verifies the test70 rollback and `Review71.epub`,
which exercises divider layout and clipping. The image was staged after the
test70 device check and a fresh reading-state backup. The
[morning check](NEXT_DEVICE_QA.md) starts with this image.

### Local test72 package

Test71 and its test70 rollback are on the card. Test72 is a separate Mac
package, `2026-11-24-test72-reading`, with test71 as its rollback. The
[device checklist](NEXT_DEVICE_QA.md) still starts with test71.

Test72 includes faster clipping saves, fewer highlight and chapter metadata
lookups, aligned GIF decoding, and optional Wi-Fi render timing summaries.
Three archived clipping stores with 14 clips reopened in memory. Adding a
sample produced byte-identical files on test71 and test72. Every prior
passage reopened after 116 simulated SD read and write failures. FAT and
exFAT also reopened all 1,024 clips in the maximum-count fixture. A card
with an unreadable primary clipping store can display a verified backup;
edits wait until the primary recovers.

The reviewed firmware source is `16da4a2b`. The packaging checkout
`5d6c28aa` adds upstream release discovery and its host test. Both X4 Pro
profiles compiled. All 2,583 enabled host checks passed in the regular and
sanitizer builds. The separate stable OTA simulation accepted the valid
compiled preview and rejected a one-byte change before boot selection.

`READER72.BIN` is 5,865,184 bytes and reports
`2.0.0-chad.3-test72-reading-x4pro`. Its board, OTA slot size, image
checksum, and validation hash passed. SHA-256:
`fa00b61162619c88cb71b6d14e09b5f117a02f4d50e6b63fdcb0967c2af9116c`.
On-device latency and release acceptance remain open.

### Local test73 package

Test73 includes test72. The card still has test71 for the first on-device check.
Test73 fixes a race between clipping saves and queued reader redraws, skips
duplicate progress writes, and reuses the path buffer while scanning Saved Items.
The reviewed source is `4f2393a9`. Both X4 Pro profiles and 2,591 enabled
checks passed in each of the regular and sanitizer host builds. The separate
stable OTA simulation accepted the compiled preview and rejected its damaged copy.

All 110 archived EPUB positions round-tripped with their original bytes. A host
scan of 1,935 catalog paths produced the same paths and eliminated 5,549
temporary allocations. The plugin host script, advertised API and capabilities,
and all ten plugin HTTP paths and methods match CrossPoint 2.0.0.

A full exFAT card can now report a failed allocation and mount again. All 94
clipping edits refused for space in the 300-case FAT/exFAT test succeeded after
space was freed. A separate deletion check exposed a remaining limitation. With
0–3 free clusters in the test images, a deletion could leave an incomplete move
journal. The original book stayed readable in the image and no committed
deletion was recognized. The existing recovery gate can block reader entry in
this condition. These tests used simulated cards.

`READER73.BIN` is 5,865,632 bytes. Its ESP32-S3 board tag, personal version,
OTA slot size, image checksum, and validation hash passed. The Mac package
`2026-11-24-test73-reading` also contains the known-running test70 rollback,
the SDK patch, source revision, test evidence, and checksums. Image SHA-256
`ac3c817e63a5bcc513c9bf73e9718b3ab10517f135130b80d3a8b08dd510d1e0`.
Physical clipping latency and display behavior remain open.

### Local test74 package

Test71 remains the next device check. The Mac package
`2026-11-24-test74-cardspace` builds on test73. Test74 checks free clusters
before staging an EPUB deletion for library sync. Four review areas covered
correctness, embedded behavior, architecture, and documentation at `f964b3bf`.
Library and File Browser report insufficient space, and Wi-Fi File Manager
identifies which files in a batch need a retry.

On simulated FAT and exFAT cards, 186 of 328 attempts completed immediately.
The other 142 wrote nothing before refusing and succeeded after freeing space.
After remount, all 328 held books and receipts verified. Test inputs included
long names, crowded directories, and zero through 40 free clusters. An almost
full or fragmented FAT card may need to read its entire allocation map. Its
duration on the X4 Pro and device-to-library deletion and restore remain open.

The pre-test71 backup supplied 50 sealed book-move records, 96 per-book
statistics records, and two global statistics records. All round-tripped byte
for byte. The CrossInk layout decoder read all 30 legacy per-book settings
files. The archive remained read-only.

Both X4 Pro builds and all 2,591 enabled host checks passed in regular and
sanitizer configurations. The separate offline OTA check accepted the compiled
stable preview and rejected a corrupted image. All eight files in the private
Mac package verified, including the test70 rollback. The personal image is
5,867,200 bytes with SHA-256
`77317b17a7d88e5a1f6b2ed1f1fd9add9f3b093427b0efee0dec4f19a782209e`.

### Local test75 package

Test71 remains staged for the morning check. Test75 at `23aeeaaf` adds
recoverable deletion for long UTF-8 EPUB names. The reader keeps files that
exceed the receipt path or size limit. File Browser shows readable siblings
beside unsupported names and asks for a retry after a directory read error.
The Wi-Fi file list ends an incomplete response on a read or encoding error.
The SdFat upgrade checker passed all 41 upstream and migration paths.

Ten loopback cases covered FAT and exFAT using firmware storage, receipt,
listing, and checksum code with the library's sync and recovery code. A
local HTTP adapter and temporary library folders stood in for Wi-Fi and
Drive. The 108,921-byte fixture covered archive and restore, a lost
acknowledgement, a restored reader copy, a 745-byte Unicode book name, and a
765-byte folder name. Two review rounds covered storage, resources,
correctness, and documentation. After the second round, focused regressions
verified the final omission of unsupported native names and rejection of
unrepresentable Wi-Fi names.

A separate restore-and-resync check found a library issue. All ten EPUBs
came back byte for byte. In the four same-path cases, the restored file
retained access to its saved position. In the six renamed or relocated
cases, the library uploaded it under its Drive name. The ten-byte position
file survived under the original reader path.

Both X4 Pro builds passed, with 2,592 enabled checks in each regular and
sanitizer run. The offline OTA check accepted the compiled stable preview
and rejected the damaged copy. The pinned SDK's ZIP, license, protected-book,
and X4 Pro manifest tests passed on synthetic inputs. The tagged upstreams
still report CrossPoint 2.0.0 and CrossInk v2.0.0-rc3.

The Mac package `2026-11-24-test75-library-paths` includes the known-running
test70 rollback. All eight manifest entries verified. The new image is
5,867,952 bytes. SHA-256:
`f2b34f1d9438de1c9b0597847c02005b6f26b953fc78a7839995ab2d1a6f93b3`.
Hardware acceptance and the disposable-book deletion check remain open.

### Local test76 package

Test71 remains the morning install. Test76 at `976134cf` reduces the SD reads
needed to scan Saved Items. With the archived 1,935-book index, both simulated
filesystems made 1,509 fewer sector reads and produced the same paths in the
same order. Four review areas covered grouping, failure handling, legacy
compatibility, storage, and UI memory. [Measurement details](READING_PERFORMANCE.md#saved-items-feedback-november-12).

Both X4 Pro profiles and all 2,595 enabled host checks passed in regular and
sanitizer configurations. The offline OTA check accepted the compiled stable
preview and rejected a corrupted copy. All eight files verified in the Mac
package `2026-11-24-test76-saved-items`, including the test70 rollback.
The 5,868,560-byte image has SHA-256
`8b02b2987bb05437fcaa746f4832aa2effe86eb3d6e586ac904a3493cf14765d`.
Device timing remains open.

In a separate library experiment, using the verified deletion receipt's original
reader path restored the EPUB and its exact ten-byte reading position in all ten
FAT/exFAT cases. The production library still uses the Drive path when it
resyncs the book.

## Test59 configuration

The intended device image is `2.0.0-chad.3-test59-x4pro`, built with
`pio run -e x4pro-chad-test`. The tracked environment in `platformio.ini`
enables the twelve selected experimental flags listed below and inherits the
X4 Pro board, USB, content-protection, and logging configuration. Its resolved
flags match the checked twelve-flag WIP environment. Resolving the tracked
configuration without `platformio.local.ini` produces the same flags, so this
candidate needs no personal override file. Inventory/staged-publication
prototypes remain disabled. A unique version is not installation approval.

The frozen pre-notice profile built in 188.69 seconds (RAM 113100, flash 5804822).
The application image has the ESP32-S3 chip ID and expected version string.
The full host build passes after adding the shared-validator include path to
its daily-statistics variant. Logs are `/tmp/x4-test59-build.log` and
`/tmp/x4-test59-host-build2.log`. The cumulative host run and targeted second
review results are recorded below for this source/configuration checkpoint.

## Requirement and frozen inputs

Implement Chad's 28 selected Now feature cores from
[the parity table](FEATURE_PARITY.md), preserving CrossPoint 2.0.0 reading,
plugin/protected-book interfaces, and existing reading state. Later features
remain deferred; Nearby is skipped. Device performance tuning follows feature
integration, with measurements required before claiming improvement.

- Second-round frozen test59 source: `d93dce0ceaf73c4a40cef83502df2fdea65fe42b`.
- Second-round source tree: `9364f092acac507bed91386193149ddf84080fb8`.
- Final packaged firmware source: `d8d6005a1d5dfae3be95507babe7f9b271800d70`,
  tree `6c217f59776831c7691f3caa74b5d3f6376cfb66`.
- Historical round-one source: `f3b28c79a4b75236404c3965dab3cefc4fcbfcb1`,
  tree `0e7f79cc951f152fdda9864dce65f0742013a0c5`.
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

The tracked test59 profile and historical local WIP recipes are described in
[development process](DEVELOPMENT_PROCESS.md).
Inventory, stage, and publication prototype flags are absent. D1 uses the
existing selected-book app, ordinary exclusive upload, and checksum/download
verification. The default and release profiles still leave WIP options disabled.
The frozen build identifies as test59 and uses the tracked `x4pro-chad-test`
profile. Packaging and device acceptance remain separate gates.

## Recorded checks

| Check | Evidence and limit |
| --- | --- |
| Historical round-one default X4 Pro build | At `f3b28c79`: 71.77 s; RAM 113068, flash 5696342. Log `/tmp/x4-create-upload-default.log`. |
| Historical round-one twelve-flag X4 Pro build | At `f3b28c79`: 76.96 s; RAM 113100, flash 5803398. Log `/tmp/x4-create-upload-enabled.log`. |
| Frozen test59 X4 Pro build | At `d93dce0c`: 188.69 s; RAM 113100, flash 5804822. Log `/tmp/x4-test59-build.log`. These are build footprints, not runtime peaks. |
| Update path | 43 focused checks and both sanitizers pass. Real host pinned-root API/redirect/CDN and rejection checks pass; firmware TLS cryptography and runtime resources remain untested. See [update contract](PERSONAL_UPDATES.md). |
| Sync client | Eight full-queue HTTP cases; all 303 canonical tests and 68 concepts pass. Code-only publication passes 73 tests and five base concepts. These use a host reader fixture. |
| Exclusive upload | The original round-one run reported vendor UBSan failures. The corrected pinned dependency and locked create-only path now pass real FAT/exFAT checks under both sanitizers using target bytewise helpers. See the retained-fix section below and [sync contract](FILE_CHECKSUM_API.md). |
| Historical round-one host suite | `f3b28c79` passed all 2431 checks in 447.16 s. Logs: `/tmp/x4-candidate-host-build.log` and `/tmp/x4-candidate-host-tests.log`. |
| Test59 host suite | The cumulative run passed 2446/2448 checks in 137.87 s. The two failures were host include/link adapters. Their corrected rerun passes both checks; the other 2446 inputs are unchanged. Logs: `/tmp/x4-test59-host-tests.log` and `/tmp/x4-test59-host-adapters.log`. No firmware change was needed for these harness corrections. |
| Physical reader | Clipfix2 is the last accepted baseline. No later source in this packet has been installed or physically validated. |

## Round-one findings and corrections

All four independent axes completed read-only review of the pinned source.
They inspected central contracts and selected callers; this was not exhaustive
line-by-line inspection of all 459 production/build/script paths. No reviewer
ran tests, builds, writes, or device operations. The findings below are not
resolved by the passing pre-correction suite.

| ID | Finding | Current status |
| --- | --- | --- |
| R1, P1 | Per-book JSON retries can delete a `.tmp`/`.bak` alias sharing the live primary's chain. | Corrected under one storage lock with checked lookups, disjoint-chain checks, exclusive staging, sync/close/readback, and checked rename results. Real FAT/exFAT alias regressions fail before the fix and pass afterward. Verified in the second cumulative review. |
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
audit rejected them. New direct/deferred move requests now return UnsupportedMoveSource
before changing finished status/completed-book totals or starting a journal. Ordinary finished marking
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
bytewise helper path. The second review is complete. Host adapter fixes clear the two failed checks. The final notification correction
and its complete host run are recorded below; runtime/device gates remain.

## Second-review outcomes

All four axes reviewed frozen source `d93dce0c` against `f3b28c79`, read-only.
Correctness, embedded, and architecture found no new production defect in that
correction diff. Two host harness include/link gaps are corrected and pass their
rerun. The documentation review identified stale provenance (corrected above),
an overbroad statistics guarantee (narrowed to finished status/completed-book
totals), and a permanent unsupported-name error that still offers generic Retry.
That notification is corrected below. The reviews
carry the first round's unchanged-code coverage limits; they do not prove
physical behavior. No additional broad review round is planned.

## Final notification correction and checks

Unsupported automatic-move sources now retain a named result through the
transaction and reader adapters. Reader and closed-book Statistics show a
translated explanation naming Move Finished Books to Read Folder, with one OK
button. Dismissal does not retry. Ordinary confirmations retain their two
buttons. The Statistics path releases its render lock and temporary snapshot
before opening the notice. Failed notice allocation keeps the current screen
usable. Pending reading time can be saved; finished status and completed-book
totals remain unchanged on rejection, and normal tracking continues.

This bounded follow-up was reviewed by the main agent against all result and
confirmation callers, with no third broad review round. Native ASan/UBSan checks
cover 43 completion cases, continued tracking, normal marking without a move,
notice dismissal/allocation failure, and unchanged two-button confirmation.
The complete final host suite passes all **2451/2451** checks in **126.91 s**.
Log: `/tmp/x4-test59-final-host-tests.log`; full build:
`/tmp/x4-test59-notice-host-build.log`. The test59 firmware build passes in
108.60 s (RAM 113100, flash 5805946), log `/tmp/x4-test59-notice-build.log`.
GDB reports ConfirmationActivity growing from 900 to 904 bytes; there is no new
reader buffer, retained cache, or task. English source labels regenerate normally;
generated outputs remain ignored.

The final application image is 5811216 bytes, ESP32-S3 chip ID 9, version
`2.0.0-chad.3-test59-x4pro`, SHA-256
`adda27fc50ffe070301f8d12ee1c4a8b35cd5834e6773ee83c0bbf52cf5ae0f4`.
The package manifest records the final source checkpoint. The second-round
review source above is the pre-notice base, not the final image's source.
Official CrossPoint 2.0.0 X4 Pro rollback is downloaded and verified against its
GitHub asset digest `eec8b22b96202da31c91ec73f4af3fa9e3149658c3cf38dab7aa2b810ff34b44`.
No device installation or physical pass is claimed.

## Review and remaining gates

This candidate combines persistent writes/migrations, resource ownership,
SDK changes, input, and UI. The firmware-handoff skill therefore requires four
independent review axes: correctness, architecture, embedded, and i18n/docs.
Reviewers read the pinned code, history, and recorded checks only. The main agent
owns fixes and verification; source stays frozen during review.

- [x] Complete cumulative host checks for the round-one frozen source and record the terminal result.
- [x] Revalidate the final correction set with the full 2451-check host suite and intended X4 Pro build.
- [x] Complete all four review axes and resolve known findings; two cumulative rounds plus checked bounded follow-ups.
- [x] Reproduce and correct retained vendor UBSan findings and benign upload-collision retry fencing, with target-path sanitizer checks.
- [x] Stamp a unique test version and record a reproducible candidate profile: test59, `x4pro-chad-test`.
- [ ] Measure runtime internal/PSRAM free bytes, largest blocks, and stack.
- [x] Package source provenance, exact configuration, hashes, rollback, and the short QA plan. All seven package checksums pass.
- [x] Reidentify the reader and verify 255 accessible state files by matching second reads. The handoff records Wi-Fi coverage limits and excluded caches/credentials.
- [x] Transfer and read back `/zDev/TEST59.BIN`; verify the existing official rollback image. Chad performs installation.
- [ ] Confirm reading state, inputs, clipping, and representative new features.

The five-minute smoke test starts with a known book's resume position, clipping,
page turns/Quick Lock, one representative feature, and sleep/resume. Longer
backup/restore, completion-move, TLS-failure, and throughput checks use disposable
books and separate sessions. [The broader QA list](NEXT_DEVICE_QA.md) remains a
menu of later checks, not one five-minute assignment.

No device polling, flash, release publication, supported-branch merge, or PR is
authorized by this review record. Standing authorization covers checked WIP
checkpoints and pushes; device installation remains the reviewed human step.

### Local test77 and test78 packages

Test77 checks long filename lengths in the bookmark and clipping directories
before opening each possible match. On a copy of the 1,935-book catalog with
saved-item filenames for three books, the scan inspected seven books. Adding
an unrelated accented or emoji filename gave the same result on FAT and exFAT.
Direct filesystem lookups found every expected match. This measures work on
the host. First-open time on the reader is still open.

Test78 includes test77. When Move to Read moves a finished book, it copies
`progress.bin.bak` to the corresponding new cache alongside the recorded
primary and preserves the source. Of 110 backed-up EPUB caches, 39 had a
backup position, and 32 differed from the primary. The new plan refuses a
backup larger than 512 bytes before hashing it. Existing v2 and v3 plans
finish with their original file lists. Finish or recover an active v4 move
on test78 before rolling back. Preserving a backup file does not change
which position the reader opens.

| Mac build | Reviewed firmware source | Enabled host checks per configuration | Image SHA-256 |
| --- | --- | ---: | --- |
| Test77 | `9edc3101` | 2,595 | `847a75d588c13afe534a44a8768184edb6445e0e3ce0b47ea4b16537d59f6be6` |
| Test78 | `089801b3` | 2,601 | `0a7698643f77a89d60f704f05e21661cb53e7b19c85defd40b21da97956a6cbd` |

Both builds passed standard and personal X4 Pro compilation, regular and
sanitizer host checks, and the compiled stable-update simulation. Each
package includes the confirmed test70 rollback. Test71 is the image on the
card for the [morning reading check](NEXT_DEVICE_QA.md).

### Local test79 package

Test79 adds two reading improvements. With black-and-white images and text
antialiasing enabled, already-cached illustrations skip pixel processing in
both gray passes. The low-memory streaming path still checks SD reads.
Bottom-up BMP covers that extend below the screen now draw their visible
rows. An entirely hidden image still skips its first row read.

Saved Items reuses the same filename buffer for recovery suffixes. In six
FAT and exFAT runs on a copy of the 1,935-book catalog, all seven candidate
books matched test77. Temporary allocation calls for checking the filenames
fell from 36,744 to 23,203. Actual first-open time needs a reader measurement.

Test79 source `6b7ebdaf` passed 2,602 enabled checks in each host
configuration, both X4 Pro builds, and the compiled stable-update simulation.
The 5,869,264-byte Mac image has SHA-256
`c2ed8c3314ecdfb6000b895f98171520d4c2b055f93c958aeaae29cbeba18426`.
The package includes the test70 rollback. Test71 remains staged on the reader.

### Local test80 package

Test80 reads a book’s saved completion status without opening its tracking
switch. On a 1,935-book test catalog, both builds identified the same 57
finished titles. Simulated SD reads fell 37% on FAT and 38% on exFAT.
With EPUB timing diagnostics enabled, the report also shows the duration and
outcome of the last progress save.

Source `b2b327a1` passed 2,605 enabled checks in each host configuration,
both X4 Pro builds, and the compiled stable-update simulation. The
5,869,520-byte Mac image has SHA-256
`8328027405989f16a31b06def925685e702e86325f63b371da14b8bb27d06f0d`.
Its package includes the test70 rollback. Test71 remains staged on the reader.

### Local test81 package

Test81 handles waiting input before loading next-page fonts or extending
a partly indexed chapter. Turning beyond the last cached page requests
the next page of that chapter. A failed extension retains the prior page
for retry. EPUB and XTC progress saves also check the staged write before
replacing the current position.

Source `67e8584d` passed 2,606 enabled checks in each host configuration,
both X4 Pro builds, and the compiled stable-update simulation. The
5,869,568-byte Mac image has SHA-256
`c81ad5885715947e30a2291d2b64026ad9e5fd70d8ef7167737c840734e2de87`.
Its package includes the test70 rollback. Test71 remains staged on the reader.

### Local test82 package

Test82 verifies a leftover progress file before reusing it. Across 5,559
interrupted card snapshots, the number of retries that reported success but
reopened at the wrong position fell from 12 to zero. The checks refused 764
more retries on damaged snapshots. Normal saves used the same number of
card reads and writes as test81.

Source `6aa0dadd` passed 2,608 enabled checks in each host configuration,
both X4 Pro builds, and the compiled stable-update simulation. The
5,870,384-byte Mac image has SHA-256
`d13d3864d86931a7c7a7524973a47fccba8dd3a887c12cfa23b6b0cf4510f9ee`.
The package includes the test70 rollback. Test71 remains staged on the reader.
