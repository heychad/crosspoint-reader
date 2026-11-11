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
4. During implementation, run the checks that can expose the changed behavior.
   Add tests for a meaningful regression or risky contract, not source wording.
5. Checkpoint and push reviewed work on the authorized personal branch. A WIP
   commit may identify disabled or incomplete work; it is not a release claim.
6. Prepare a cumulative device candidate with the full suite, intended X4 Pro
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
