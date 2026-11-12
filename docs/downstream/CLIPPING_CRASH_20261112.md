# Test58 clipping crash repair

Feature work is paused while Chad tests clipping stability. This candidate is
based on installed test58, `6265ad4372673047a4e5045355b45898c090995a`, and excludes
the newer feature branch. It changes selection initialization and crash
diagnostics; it does not change book, clipping, settings, or progress formats.

## Evidence

- The reader at `192.168.86.175` reported
  `2.0.0-chad.3-test58-x4pro`. Its saved report records a PANIC after a clipping
  selection reached the existing 240-word cap. The cap alone does not establish
  the cause.
- The report lacks the faulting PC and exception registers. Stack return
  addresses include the render task, but do not identify the failing instruction.
- The archived test58 image has SHA-256
  `4219c1af390f05f82db296d9ddfe30a50b6f2e6202c8b6ffa057c74aba7ae09a`.
  Its embedded ELF digest matches the preserved ELF with SHA-256
  `0f4e36e9833879e1e0adc74075f6817ca53e8aa12b53bb4c8ce57cad13e93420`.
  The installed flash was not read back; its reported version matches test58.
- Local evidence is preserved outside Git in the release worktree's ignored
  `.cache/clipping-crash-20261112/` directory.

## Repair

ActivityManager can publish a new activity before its `onEnter()` finishes. A
queued paint could therefore use an uninitialized clipping selection or overlap
font-cache setup. Selection initialization now holds the existing render lock.
An atomic readiness flag blocks paints before initialization, and a final guard
keeps an empty selection from painting after an error popup closes. Failed
initialization still cancels or presents the existing dictionary error.

A host reproducer fails on the old implementation and passes with these guards.
This establishes a lifecycle defect, not the cause of the observed device panic.
The repair needs the short device check before clipping stability is confirmed.

Crash reports also retain each Xtensa core's PC, exception cause, fault address,
return address, and stack pointer. These records use 48 bytes of retained RTC
memory and no new heap allocation. They survive a corrupt stack pointer. The
panic wrapper remains in IRAM. No new task, full-screen buffer, or storage
writer is added.

## Recorded checks

- `ClipSelectionReady` covers queued paints, initialization exclusion, cancelled
  empty selection, and the dictionary allocation-error popup.
- `PanicRegisterCapture` covers both cores, invalid stack pointers, abort epochs,
  null frames, and clearing retained metadata.
- The cumulative host suite ran 1,304 checks in 76.49 seconds. One source-extracted
  button test initially lacked the new readiness member. After its harness was
  updated, all four affected selection and panic tests passed in 3.68 seconds;
  the other 1,303 checks had passed in the cumulative run.
- X4 Pro compilation passed in 198.14 seconds: static RAM 112,868 bytes, flash
  5,575,994 bytes. GDB places the panic wrapper in `.iram0.text` and the retained
  register array at `0x50001210`.
- Local build override: `[crosspoint] version = 2.0.0-chad.3-test58-clipfix1`.
  The pinned SDK is `5e94fe7480f019c89fbf691aef87523eb1b46704`, with no SDK edits.

Logs are in `/tmp/x4-clip-hotfix-{build,full-tests}.log` and
`/tmp/x4-clip-{ready-before,ready-after,panic-tests}.log` on the build Mac.

## Reviewed device candidate

All four independent reviews of `75b95f7e4f518366e479ccf702258bdcc242e028` are
clear: correctness, architecture, embedded constraints, and i18n/documentation.
They requested no changes. The reviewers confirmed that the pinned ESP-IDF
stalls other cores and reports their captured frames sequentially, so the panic
register mask does not add a concurrent writer in that path.

The final `pio run -e x4pro` passed in 47.28 seconds, reusing valid build-cache
outputs. RAM and flash totals match the earlier build. The copied binary's
CrossPoint version strings identify `2.0.0-chad.3-test58-clipfix1-x4pro`; its
embedded ELF digest matches the archived ELF. The ESP app descriptor's separate
version field is `ee57070`, not the displayed CrossPoint version.

Wi-Fi backup verified 241 state files, 426,697 bytes. It includes reading
positions, per-book preferences, settings, bookmarks, statistics, and the legacy
statistics backup. Every copied file matched a second remote read and local
SHA-256 verification. Credentials, plugins, and derived caches are excluded.
It is not an atomic SD snapshot. The receipt is in the ignored
`.cache/clipping-state-backup-20261112/backup-receipt.json`.

`CLIPFIX1.BIN` was uploaded to a new SD path and downloaded back. Both copies
are 5,581,264 bytes with SHA-256
`055f72f889bc1ed0341138d0fc22daf46db452e9543f706d0132692eb2d4a59f`.
The matching ELF has SHA-256
`92b61f101c92b92876b921e20ab28e8d6bbe34dd6646a309a470a72158ae3410`.
The ignored `.cache/clipping-hotfix-20261112/` bundle retains the image, ELF,
readback, checksums, and manifest. No repair firmware has been installed yet;
the device crash remains unconfirmed as fixed. Feature work stays paused.

Run the [five-minute device check](FIVE_MINUTE_DEVICE_QA.md) after the reviewed
candidate is installed. If it reboots, preserve the new report and decode it
against this candidate's archived ELF before continuing feature work.

## Development branch integration

The repair is also carried into `codex/x4pro-release-baseline-20261111`, so the
next feature build retains it. The six transferred production and test files
match the reviewed hotfix exactly. The four affected host tests pass on this
branch. Its X4 Pro build passed in 82.19 seconds; logs are in
`/tmp/x4-clip-main-{focused,integration-build}.log`. Its cumulative firmware
includes later features and is not the isolated
device candidate described above. Do not deploy it as part of this repair.
The existing SDK changes remain outside this checkpoint.
