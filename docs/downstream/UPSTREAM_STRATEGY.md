# Upstream maintenance strategy

Checked November 11, 2026.

## Release channels

Chad selected stable releases and tagged release candidates on November 11,
2026. Do not track or automatically import either project's moving development
branch. Stable CrossPoint is the supported firmware base. CrossInk supplies
selected features from stable or RC tags, never a wholesale tree merge.

Current pins verified through GitHub Releases:

- CrossPoint stable: `2.0.0`, `42d58daaabcf469a2e5e1ba871714fb9dcf51d29`.
- CrossInk stable reference: `v1.6.1`, `9914146eeae7b46b300f475a16c32426fc02ec1f`.
- CrossInk RC reference: `v2.0.0-rc`, `b8feaec4d10ecb484ed80e326f01f2a3ea949331`.
- CrossPoint `2.0.0rc` is older than stable `2.0.0`; it is not an upgrade target.

Review new releases at integration milestones. Pin the selected tag and SDK,
review its changes, and test on a development branch. Promote an RC only after
our relevant checks and device smoke test; installing on the reader remains a
separate handoff. An unreleased fix needs a specific defect and explicit scope
decision, not a general invitation to follow development.

The existing branch already contains 45 CrossPoint development commits merged
at `6bea3810`, plus selected work compared with CrossInk development. This policy
does not erase that history or establish a release-only baseline retroactively.
The new working branch excludes that development series and replays our later
work. Passing build and test coverage is recorded in RELEASE_BASELINE.md. Do not reset or rewrite
the current branch to hide the discrepancy.

## Repositories and baseline

- Personal fork: https://github.com/heychad/crosspoint-reader
- Upstream: https://github.com/crosspoint-reader/crosspoint-reader
- Feature reference: https://github.com/uxjulia/CrossInk
- Pushed feature checkpoint: `codex/x4pro-completion-integration`, `9c98e395`.
- Preserved development-based branch: `codex/x4pro-upstream-20261111`.
- Current working branch: `codex/x4pro-release-baseline-20261111`; see [reconciliation](RELEASE_BASELINE.md).
- Stable baseline: CrossPoint 2.0.0, `42d58daaabcf469a2e5e1ba871714fb9dcf51d29`.
- Previously integrated upstream development tip: `0333cbbe4`, 45 commits beyond the stable baseline.
- Original CrossInk reference: v1.6.1, `9914146ee`.
- Active CrossInk feature comparison: v2.0.0-rc, `b8feaec4d`. Historical development audit: `1990a78f2`.

These are dated observations, not moving version pins. Refresh before integration.

## Integration policy

Keep CrossPoint as the upstream code base. Import selected CrossInk behavior in
small commits; do not merge the whole CrossInk tree. Preserve the X4 Pro-only
release target while retaining upstream source layout where practical.

The 45-commit CrossPoint update was merged at `6bea3810`. Do not repeat that
completed integration before every feature batch.

At the next integration milestone, run:

```sh
python3 scripts/upstream_snapshot.py --fetch
```

Inspect newly reported changes, prioritize fixes to already-ported behavior, and update the comparison baseline after recording
what was reviewed. CrossInk remains a source reference, not a merge target.
Use the [development process](DEVELOPMENT_PROCESS.md) for verification depth.
Keep the known device-tested revision available for rollback.

## Ongoing maintenance

- Check both CrossPoint and CrossInk at each integration milestone and before a
  firmware release. Compare stable releases and tagged release candidates; record
  the exact commits reviewed.
- For each CrossInk change, check whether CrossPoint already includes it, whether
  our port still needs it, and whether it belongs to the agreed Now or Later
  scope. Fixes to already-ported features take priority over new features.
- Prefer upstream fixes over maintaining equivalent custom patches.
- Keep reader-specific policy and plugins separate where the existing APIs fit.
- Preserve the SDK submodule pin; store necessary local SDK changes as reproducible
  patches in this repository. Review those patches when updating the SDK.
- Keep experiments disabled for release until reviewed and tested.
- Use WIP checkpoint commits for backup. Releases and supported-branch merges
  require risk-based review and device handoff as defined in the handoff skill.

## Current limitations

The feature checkpoint is committed and pushed. The upstream merge passed the
X4 Pro build; 1,774 of 1,779 host tests passed initially, and focused reruns passed
after correcting five stale fixtures. A later full suite passed all 1,784 tests.
This is not a firmware release. Experimental features in the checkpoint remain
incomplete or unverified. The feature checklist is not proof
of full CrossInk parity or release readiness. CrossInk's new release-candidate SDK submodule could not be fetched
from its referenced upstream object during this check; the top-level source was
fetched successfully. Do not substitute another SDK revision without review.

## CrossInk follow-up audit

Compare stable `v1.6.1` with `v2.0.0-rc` (`b8feaec4d`). The previous
development audit at `1990a78f2` remains provenance, not an ongoing target. Do not assume the old v1.6.1 parity inventory covers them.

Priorities from the current source comparison:

- IPC task stacks: no extra wrapper needed for the current X4 Pro build. Its
  linked `esp32s3/dio_opi` SDK already sets `CONFIG_ESP_IPC_TASK_STACK_SIZE=2048`;
  `esp_ipc_init` in its `libesp_system.a` passes 2048 to task creation. The 1024
  setting in `platformio.ini` applies only to the unused C3 profile. Recheck the
  linked SDK when upgrading dependencies.
- Compare sync error handling and position recovery before extending sync.
- Compare fixes to dashboard covers, Carousel placement, and grouped EPUB
  headings against equivalent CrossPoint changes to avoid duplicate patches.
- Reconcile RC status-bar sizing, battery modes, screen calibration, and
  chapter shortcuts with the selected feature inventory.
- Keep later-scope fonts, dictionary additions, and nearby sync out of the next
  release batch unless the human changes scope.

## Per-book settings format audit

CrossInk release candidate `v2.0.0-rc` (`b8feaec4d`) writes per-book reader-settings version 13.
The decoder now accepts versions 1–13. Versions 11–13 add image
grayscale, character spacing, and publisher-font fields after the override mask;
the SD font mask bit also moves from 18 to 19 in version 11. The update follows
the actual writer and reader, preserves unsupported fields in the preview, and
checks each version's size and override-mask boundaries. Do not interpret a
new-format file as version 10 or overwrite its source.
