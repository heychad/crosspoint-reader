# Release-baseline reconciliation

November 11, 2026. Candidate work, not an installed or approved release.

## Candidate

- Working branch: `codex/x4pro-release-baseline-20261111`.
- Starting fork checkpoint: `9c98e395`, based on CrossPoint stable `2.0.0`.
- SDK pin: `5e94fe7480f019c89fbf691aef87523eb1b46704`, matching the stable tag.
- Preserved branch: `codex/x4pro-upstream-20261111`, checkpoint `c6e1afbf`.

Replayed the 14 first-parent project commits after merge `6bea3810` onto the
pre-merge fork checkpoint. The merge itself was excluded. Translation conflicts
retained the new project labels without importing unrelated development labels.
The policy document conflict retained the later policy and is reconciled here.

A Git ancestry check found zero of the 45 commits in
`2.0.0..0333cbbe4` reachable from this candidate. This proves the history excludes
that development series; it does not prove every retained custom behavior has
upstream release parity or hardware validation.

## Dependency adjustment

Removed the Quick Actions ownership entry for upstream's unreleased double
power-button click. Stable CrossPoint has neither that field nor its dispatcher.
The existing Home double-tap and other selected project shortcuts remain.
The original development-based branch preserves the removed integration for
reference; it has not been reset or rewritten.

CrossInk `v2.0.0-rc` declares reader-settings format 13 in
`src/activities/reader/EpubReaderActivity.cpp`. The imported formats 1–13 decoder
therefore remains relevant to the tagged RC, rather than depending on a moving
development branch.

## Validation

The X4 Pro build against the stable SDK passed in 208.08 seconds. The full
1,775-test host run passed 1,773 tests and found two stale expectations for the
recent Home enabled-state import. Those fixtures were corrected; all 22
focused controls/Quick Actions tests then passed, including both corrected
cases. No production edit followed the successful firmware build.

This is the working development baseline, not a release approval. Use
independent review and the device handoff before release or installation. Experimental feature flags
still need their intended configuration tested; a default build does not prove
every combination works.
