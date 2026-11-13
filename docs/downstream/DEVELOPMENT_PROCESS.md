# Development process

Effective November 11, 2026. Chad requested faster development and easier
maintenance across CrossPoint and CrossInk. This policy replaces per-feature
release ceremony with checked development batches and cumulative device tests.

## Work loop

1. Compare stable and tagged RC releases at integration milestones, before a port
   likely to duplicate upstream, and before preparing a release.
2. Group related work around a shared reader, input, library, or statistics path.
   Aim for a useful set of behaviors per device test, not one setting per build.
3. Before changing file deletion or replacement, identify every live owner of
   those files and the existing close/exit path. Reuse that path before adding
   storage work. For reader caches, a released section is not enough: EPUB
   metadata holds file handles until the reader is destroyed.
4. Before growing frequently copied reader state, inspect by-value copies and
   their stack cost. The daily recorder would have made a statistics preview
   clone 412 bytes; a const projection removed it without another allocation.
5. During implementation, run the checks that can expose the changed behavior.
   Add tests for a meaningful regression or risky contract, not source wording.
6. Checkpoint and push reviewed work on the authorized personal branch. A WIP
   commit may identify disabled or incomplete work; it is not a release claim.
7. Prepare a cumulative device candidate with the full suite, intended X4 Pro
   build, risk-based review, and a short smoke test. Let device feedback guide
   the next batch.

Default working budget: after 20 minutes stuck on one unresolved edge case,
identify whether it blocks normal behavior or only an experimental feature.
Fix a blocker; otherwise record it and isolate the feature so the rest advances.
This is a decision point, not permission to ship a known defect. Do not add
failure models, abstraction layers, or compatibility targets outside the agreed
scope just to satisfy a speculative concern.

## Verification triggers

| Work | Required before checkpoint |
| --- | --- |
| Prose or process rules | Scoped diff, links/consistency, secret/file scan |
| Host tool | Run it on representative input; check errors that could mislead the user |
| Bounded firmware behavior | Focused checks and incremental X4 Pro build |
| Shared contract or upstream merge | Affected tests, full host suite, X4 Pro build |
| Device candidate or release | Cumulative suite, intended flags, handoff review, device smoke test |

A documentation edit does not invalidate a firmware build. A fixture-only fix
requires its affected tests, not another whole suite. A failed check is resolved
or recorded explicitly; it cannot be relabeled green. Use the existing build
cache. Preserve reading-state tests when writes, paths, formats, or migration
change. Keep default and experimental build evidence distinct.

Choose the build that compiles the changed code. A change outside feature flags
needs the default X4 Pro build; add an experimental build when changed code is
hidden behind its flags or changes their integration. Continue running the
relevant host flag combinations. The next device candidate still needs a full
build with its exact intended flags. Do not describe an older experimental
build as validation of the current revision.

For reader-menu changes, run `StatsReaderMenu`, `ReaderTabbedMenu`,
`ReaderInlineTextMenu`, and `XtcReaderMenu` together before the full suite.
Both the older menu fixture and the newer tabbed fixture exercise the same
production menu; testing only the newer one can defer a row-count failure until
the full run. Use the existing CTest build with
`ctest --test-dir <host-build> -R 'StatsReaderMenu|ReaderTabbedMenu|ReaderInlineTextMenu|XtcReaderMenu' --output-on-failure`.

Review depth is defined in
[firmware-handoff](../../.agents/skills/firmware-handoff/SKILL.md). Four reviews
are for broad integrations, not every small feature. Standing design acceptance
and checkpoint/push authorization remain valid for this project.

Source-extraction regression tests should select the target method's body,
not everything before an unrelated neighboring method. The latter made two
time-left tests fail when the dictionary picker was inserted; both now stop
at the target method's closing brace.

## Maintenance cost

Keep CrossPoint's structure and SDK pin. Prefer an upstream fix or existing
handler over a second implementation. Put personal policy in small downstream
modules where the existing API permits; do not refactor healthy upstream code
just to make the diff look tidy. Keep SDK patches explicit and reproducible.

Use `python3 scripts/upstream_snapshot.py` for a local comparison, or add
`--fetch` at an upstream checkpoint. It reports exact tips, unmerged CrossPoint
commits, CrossInk changes since the selected baseline, and overlapping paths.
The defaults are pinned release tags, not development branches. Update pins
after reviewing a new release. Non-release refs require an explicit audit flag.
Path overlap is a triage signal, not proof of conflict or missing functionality.
Review selected changes; never automatically merge CrossInk wholesale.

## Measure the loop

Record each completed batch's focused-test, full-suite, and build durations
from tool logs. Reuse a passing result until its inputs change; documentation
and test-only edits do not require rebuilding unchanged firmware. Report
rework separately so faster compilation does not hide avoidable design work.

At each device milestone record elapsed implementation, verification, review,
and blocked time from available logs, plus completed user-visible behaviors.
Do not infer coding speed from commit timestamps or claim a model/tier speedup
without comparable measurements. Current observed checks take minutes; the
larger opportunity is reducing repeated analysis, excessive scope, and waiting
for a perfectly complete feature before testing useful work.

Update the progress Page once per meaningful milestone. Distinguish built,
reviewed, device-tested, and deferred. Never turn unknown into complete.

## Measured checkpoints

These are check durations, not coding speed or total development time.

| Batch | Host checks | Default X4 Pro build |
| --- | --- | --- |
| Live book preview; shared layout changes | Full 1,833-test suite: 68.53 s | 127.59 s |
| Four compatible preference imports | 10 affected checks: 2.23 s | 68.31 s |
| Legacy book rendering profile | 95 affected checks: 0.91 s | 67.14 s |
| Carousel geometry | 47 affected checks: 1.84 s initially; corrected fixture: 0.52 s | Carousel-enabled profile: 190.30 s, first-use setup included |
| Daily recorder and shared preview | 410 focused: 0.67 s; cumulative 2,227 cases: 47.90 s, one fixture corrected in 0.65 s | Enabled profile: 80.00 s final; initial 178.74 s preceded stack correction |
| Independent margin settings and migration | 91 focused: 12.24 s; cumulative 2,240 cases: 242.39 s, one web fixture corrected alongside two added checks in 1.49 s | 65.85 s final; initial 112.27 s preceded resave correction |
| November 12 named font migration | 79 focused checks: 4.16 s, including 3,343 decoder records | 76.98 s |
| November 12 Safe Mode preservation | Cumulative 2,256 checks: 78.40 s; two additional ownership/profile checks: 0.07 s | 141.06 s |
| November 12 sleep transition | Cumulative 2,259 checks: 82.37 s; stale-input regression checked with and without Page Overlay | 134.95 s |
| November 12 library-policy import | 136 focused: 5.59 s; sanitizer and Carousel mapping checks passed | 66.81 s |
| November 12 legacy reader bars | 148 focused: 10.88 s, including 5,832 combinations per flag configuration; sanitizer coverage passed | 71.23 s; combined eight-feature profile: 164.05 s, first build of that profile |
| November 12 RC2 bar controls | 2,290 full: 73.26 s; 12 bar checks including the added malformed-style case: 0.09 s; sanitizer render checks passed | 128.61 s; combined eight-feature profile: 109.84 s |
| November 12 completion queue and Retry | 351 focused: 91.13 s serial; five final queue/lifecycle checks: 0.72 s with six workers; sanitizer checks passed | Automation-enabled profile: 182.26 s first setup; 75.56 s final |
| November 12 published-reference recovery | 2,301 full: 72.08 s; 18 affected including native store integration: 3.59 s; integration sanitizer checks passed | 125.21 s; automatic-completion profile: 108.68 s |
| November 12 sleep recovery after completion | 206 affected: 28.87 s; native state/Recents and access-gate harness passed sanitizers with Page Overlay off/on | Automatic-completion profile: 71.48 s; standard build unchanged outside the Page Overlay gate |
| November 12 native move coordinator | FAT/exFAT, both Recents policies, real transaction probe and lease: 34.65 s; original journal profile: 28.15 s; AddressSanitizer passed; vendor UBSan findings reproduce in the older profile | Not repeated: tests and documentation only |
| November 12 moved bookmark/progress readers | 96 affected: 35.34 s; final native reader check: 36.10 s; four binary bookmark versions and all three progress lengths; AddressSanitizer passed | 67.41 s; automatic-completion profile: 67.71 s |
| November 12 native bookmarks, clippings, and Saved Items | 47 affected: 38.79 s; production save/load and per-book discovery, post-move edits, opaque sidecars; AddressSanitizer passed | Not repeated: tests and documentation only |
| November 12 live-reader completion | FAT/exFAT, both Recents policies, undo, recovery, exact reading totals, and saved-state reopen: 42.52 s; daily profile: 43.13 s; AddressSanitizer passed | Not repeated: tests and documentation only |
| November 12 ordinary-file checksum API | 63 affected: 4.52 s; new unit and HTTP paths passed AddressSanitizer and UndefinedBehaviorSanitizer | 72.74 s; first attempt failed in 40.33 s on an Arduino macro collision, then corrected |
| November 12 prompt-to-move integration | Full 2,350-test suite: 126.77 s; final native prompt/default and daily profiles: 46.15/47.05 s; AddressSanitizer passed | Nine-flag automation profile: 85.52 s; default firmware inputs unchanged since the checksum build |
| November 12 older preference names and Library rows | 112 affected: 2.54 s; Preferences/Library staging passed AddressSanitizer and UndefinedBehaviorSanitizer | 70.12 s; default X4 Pro; nine-flag profile not repeated for these flag-independent mappings |
| November 12 Library view persistence and exact sort imports | 205 affected: 3.92 s; staging and view-change paths passed both sanitizers; older tab-lock fixture updated | 106.43 s; no experimental-only path changed |
| November 12 first-name index order | 2353 full: 92.69 s; 37 native builder/sorter cases passed both sanitizers | 85.32 s; UI selection remains a separate step |

The import batch reused the existing transaction and validation paths. Its scope
did not justify repeating the full suite or an experimental firmware build.
The next cumulative device candidate still requires its exact enabled profile
and full-suite results.

For Home geometry changes, include `SavedItemsHomeInput` and
`SavedItemsNavigation` alongside the Carousel/Home checks. Both extract Home
methods; the navigation fixture also needs new sizing helpers even though its
name does not contain Home. Fixing that fixture does not invalidate firmware
or other passing host results.
