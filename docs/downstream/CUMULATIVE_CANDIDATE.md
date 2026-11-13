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
| R1, P1 | Per-book JSON retries can delete a `.tmp`/`.bak` alias sharing the live primary's chain. | Open; needs checked transaction ownership and real FAT/exFAT alias regressions. |
| R2, P1 | Completion accepts deep existing book paths, but its later source-reference audit applies staged-publication's 16-component limit after moving files. | Open; separate source validation from publication policy and exercise the production guard through move/remount. |
| R3, P2 | Routine application-state saves consume permanent history slots; exhaustion blocks later saves and causes increasing scan work. | Open; distinguish verified clean-save retirement from uncertain-artifact preservation. |
| R4, P2 | Rotated Quick Lock activation and unlock resolve asymmetric side holds differently. | Corrected with the shared orientation mapping; regression failed before the fix and passes afterward across all four orientations. |
| R5, P2 | Completion shortcuts retain the old `/read` restriction despite deferred automation. | Corrected to match the menu's feature gate; disabled, legacy, and deferred profiles are tested. |
| R6, P2 | Backup/rollback instructions omit root-level completion journals, deferred intents, and history. | Open; include a coherent transaction/book/sidecar backup and restore plan. |
| R7, P2 | Dashboard month abbreviations bypass translation. | Open; use English translation keys and `tr()`. |
| R8, P3 | Minimal Home QA still describes the older screen without statistics. | Open; align checks with saved time/progress and unavailable-data behavior. |

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

The embedded review also triaged retained vendor reports. X4 Pro selects the
bytewise little-endian helper path, so the Mac's alignment reports do not follow
that target configuration. Null-pointer arithmetic in `FatFile.cpp:871` and
signed overflow in `ExFatName.cpp:42–44` remain target-reachable C++ undefined
behavior; physical consequences are unproven. No category is suppressed.
A failed exclusive writable open can also poison the mutation fence until
restart. These storage issues need resolution or an explicit supported recovery
boundary before handoff. Slow-card synchronous hashes and runtime headroom need
measurement; the review does not establish a measured watchdog failure.

## Review and remaining gates

This candidate combines persistent writes/migrations, resource ownership,
SDK changes, input, and UI. The firmware-handoff skill therefore requires four
independent review axes: correctness, architecture, embedded, and i18n/docs.
Reviewers read the pinned code, history, and recorded checks only. The main agent
owns fixes and verification; source stays frozen during review.

- [x] Complete cumulative host checks for the round-one frozen source and record the terminal result.
- [ ] Revalidate the final correction set before device handoff.
- [ ] Complete all four review axes and resolve material findings.
- [ ] Triage retained vendor UBSan findings against the target and affected paths.
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
