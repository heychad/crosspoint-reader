# Upstream maintenance strategy

Checked November 11, 2026.

## Repositories and baseline

- Personal fork: https://github.com/heychad/crosspoint-reader
- Upstream: https://github.com/crosspoint-reader/crosspoint-reader
- Feature reference: https://github.com/uxjulia/CrossInk
- Pushed feature checkpoint: `codex/x4pro-completion-integration`, `9c98e395`.
- Current upstream integration branch: `codex/x4pro-upstream-20261111`.
- Stable baseline: CrossPoint 2.0.0, `42d58daaabcf469a2e5e1ba871714fb9dcf51d29`.
- Current upstream development tip: `0333cbbe4`, 45 commits beyond the stable baseline.
- Original CrossInk reference: v1.6.1, `9914146ee`.
- New comparison required: CrossInk v2.0.0-rc, `b8feaec4d`, and development tip `1990a78f2`.

These are dated observations, not moving version pins. Refresh before integration.

## Integration policy

Keep CrossPoint as the upstream code base. Import selected CrossInk behavior in
small commits; do not merge the whole CrossInk tree. Preserve the X4 Pro-only
release target while retaining upstream source layout where practical.

Before the next feature batch:

1. Push the current checked WIP checkpoint to the personal fork.
2. Compare the 45 new upstream commits with local changes. Integrate on a
   separate branch and resolve duplicate implementations before adding features.
3. Reassess selected CrossInk features against its 2.0.0 release candidate.
   Do not silently expand the agreed first-release scope.
4. Run focused checks for conflicts, then the full host suite and X4 Pro build.
   Keep the known device-tested revision available for rollback.

## Ongoing maintenance

- Check both CrossPoint and CrossInk at each integration milestone and before a
  firmware release. Compare stable releases, release candidates and subsequent
  fixes; record the exact commits reviewed.
- For each CrossInk change, check whether CrossPoint already includes it, whether
  our port still needs it, and whether it belongs to the agreed Now or Later
  scope. Fixes to already-ported features take priority over new features.
- Prefer upstream fixes over maintaining equivalent custom patches.
- Keep reader-specific policy and plugins separate where the existing APIs fit.
- Preserve the SDK submodule pin; store necessary local SDK changes as reproducible
  patches in this repository. Review those patches when updating the SDK.
- Keep experiments disabled for release until reviewed and tested.
- Use WIP checkpoint commits for backup. Releases and supported-branch merges
  still require four-axis review and device handoff.

## Current limitations

The feature checkpoint is committed and pushed. The upstream merge passed the X4 Pro build and all 1,779 host tests, including
focused reruns of five corrected test fixtures. It is not a firmware release. Experimental features in the
checkpoint remain incomplete or unverified. The feature checklist is not proof
of full CrossInk parity or release readiness. CrossInk's new release-candidate SDK submodule could not be fetched
from its referenced upstream object during this check; the top-level source was
fetched successfully. Do not substitute another SDK revision without review.

## CrossInk follow-up audit

Compare `v2.0.0-rc` (`b8feaec4d`) and development `1990a78f2` after the
CrossPoint update. Do not assume the old v1.6.1 parity inventory covers them.

Priorities from the current source comparison:

- Audit the IPC task-stack fix in `1990a78f2` against this fork's S3 build.
  This fork still configures a 1 KiB IPC stack; CrossInk wraps task creation to
  raise only IPC tasks to 2 KiB. The prebuilt SDK behavior needs checking before
  choosing a fix. This is a candidate, not an integrated fix.
- Compare sync error handling and position recovery before extending sync.
- Compare fixes to dashboard covers, Carousel placement, and grouped EPUB
  headings against equivalent CrossPoint changes to avoid duplicate patches.
- Reconcile RC status-bar sizing, battery modes, screen calibration, and
  chapter shortcuts with the selected feature inventory.
- Keep later-scope fonts, dictionary additions, and nearby sync out of the next
  release batch unless the human changes scope.
