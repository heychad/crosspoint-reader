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
readback, checksums, and manifest. At that handoff, installation and device
verification were pending. The later failed device check is recorded below.

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

## Dragging clarification

Chad confirmed that the original crash occurred while moving or dragging the
selection. The original touch loop already holds the render lock, so the
initialization repair alone does not establish the cause of a mid-drag panic.
No second concrete defect was found in the touch state transitions inspected.

`ClipSelectionTouch` now runs the production drag loop and selection helpers
under AddressSanitizer and UndefinedBehaviorSanitizer. It checks release after
the initial long press, both handles crossing their opposite endpoint, page-end
dwell through the 240-word boundary, Back after a held touch, and 8,000
deterministic coordinate frames in portrait and landscape dimensions. The test
passes. Rendering, font-cache behavior, physical touch input, and the actual
device panic remain outside this host check. The device checklist now targets
dragging first. This test/documentation addition does not change `CLIPFIX1.BIN`.

## Clipfix1 device failure and typed reader dispatch

The device now reports `2.0.0-chad.3-test58-clipfix1-x4pro`. A long press on
book text still crashes before the clipping controls appear. Stop the earlier
QA checklist until the next reviewed candidate is available.

The new report has SHA-256
`ab48e1c012618a4d01c82006ba63f665068993902ffbb487f40af1f2649e2e6f`.
It records PC `0x420BB38F`, cause `0x1C`, fault address `0xAC000004`, and return
address `0x820BB3C8`. The matching archived ELF maps the fault to the vector
access at the start of `renderBoxBackgrounds`, called by `Page::render`.
The report, status, and reading state are preserved in the ignored
`.cache/clipping-crash-clipfix1-20261112/` directory.

ActivityManager used `isReaderActivity()` to cast a foreground activity to
`ReaderActivity` and run gesture/session code. Clipping and interval-selection
screens also report a reading context, but inherit directly from `Activity`.
The cast therefore writes reader fields into unrelated child-screen storage.
The device ELF puts `ReaderActivity::gestureController` at offset 120, where
`ClipSelectionActivity` stores its page vector. This explains a concrete path
to the invalid page pointer.

`ReaderActivityCapability` exercises the production manager dispatch and pause
branches. Before the fix, its clipping-page storage assertion fails. With the
fix, clipping and interval-selection storage remain intact, while actual readers
still receive gestures, Quick Actions, session pauses, and foreground routing.
It runs with address and undefined-behavior sanitizers.

The fix introduces `Activity::bookReader()`, which returns null by default.
Only `ReaderActivity` returns `this`. Manager code uses that typed capability
for reader-only state and controls. The broader reading-context flag stays
unchanged for context and Bluetooth screen transitions. No instance field,
heap allocation, task, reading-state format, or rendering algorithm is added.
The next candidate still needs cumulative checks, release review, a final X4 Pro
build, and a fresh device test.

The cumulative host run passed 1,305 of 1,306 checks in 77.80 seconds. The
remote-input fixture lacked the new typed accessor; after that harness update,
both affected tests passed in 1.07 seconds. All four release reviews of
`f9cf015f` are complete. They found no code defect requiring changes. The old
installation note is now explicitly historical, and the short QA includes all
four orientations. Final firmware compilation and device verification remain
pending.

## SD development folder

`TEST58.BIN`, `CLIPFIX1.BIN`, and this device failure's `crash_report.txt` were
moved from the SD root into `/zDev/`. Each destination matched its source size
and SHA-256 after download, and the three source paths are absent. The receipt
and private copies are in `.cache/zdev-migration-20261112/`. Future manually
staged firmware and development files use `/zDev/`; runtime reading-state paths
remain unchanged.

The development branch already had the `bookReader()` capability from
`9c98e395`. This checkpoint adds the manager-boundary regression and device
failure record; it does not add a second capability or change firmware source.

## Clipfix2 reviewed handoff

The reviewed source is `8b52deb14532c4582a4edf2c4446cd54de325514`, with
SDK `5e94fe7480f019c89fbf691aef87523eb1b46704` and the local version override
`2.0.0-chad.3-test58-clipfix2`. The final X4 Pro build passed in 105.27
seconds. Static RAM remains 112,868 bytes; flash is 5,576,326 bytes. GDB confirms
unchanged object sizes: Activity 120, ReaderActivity 896, and ClipSelectionActivity
964 bytes. The matching-build log is `/tmp/x4-clipfix2-aligned-final-build.log`.

All four release reviews cover the typed boundary. Correctness and architecture
also checked the final accessor-name alignment with the existing development
branch. Both aligned tests pass; the other cumulative host results remain valid.
The 240-file reading-state backup was read twice and hash-verified before staging.
Its receipt is in `.cache/clipping-state-before-clipfix2-20261112/`.

`/zDev/CLIPFIX2.BIN` and `/zDev/CLIPFIX2.sha256` are uploaded and readback-verified.
The image is 5,581,584 bytes, SHA-256
`0c94ac316af89ff06878993a631c47bae9df9204945273c5e825c7dde5aca6e3`.
The archived matching ELF has SHA-256
`e655a576ae9d6cb0ecfd0fee2dcd8f4f690eaa3a7a8b7671b18c23d42c1cd716`.
The private image, ELF, readback, and manifest are in
`.cache/clipping-hotfix2-20261112/`.

At this handoff, the installation and long-press checks were pending. The
observed device results are recorded below.

## Clipfix2 device result

Chad confirmed that selection controls open normally and that a clipping saved.
The live reader reports `2.0.0-chad.3-test58-clipfix2-x4pro`.

The SD check found one complete v4 clipping record: 36 words, 206 UTF-8 bytes,
with its book metadata, source range, and page 4 location intact. The selected
text matches the `My Clippings.txt` export. Both files matched a second remote
read. Private copies and the verification receipt are in
`.cache/clipfix2-device-check-20261112/`; the book text is not copied into Git.

The reported long-press crash and save path pass this device check. Chad also
confirmed that Saved Items shows the expected text and reaches the correct
passage, that selection cancellation works in each orientation, and that page
turns, Quick Lock, and sleep/wake preserve his reading position. Home now opens
directly to the covers. These are user-observed checks on clipfix2, not a claim
that later development features have been tested.

Saved Items took more than 30 seconds on its first open only. It currently groups
saved content by book: opening a book leads to its clipping list, then a clipping
action menu. The brief page flash before that list and first-load latency are
recorded as follow-up polish. The reader intentionally waits for a page render
before opening the saved-content list; that explains the flash. The catalog scans
the library for saved metadata, and can rebuild a stale index, but this check did
not measure which operation caused the first-load delay.
