# X4 Pro integration checkpoint

November 11, 2026. Work in progress, not a firmware release.

This checkpoint backs up the CrossInk integration work since test58. It targets
the X4 Pro and retains CrossPoint 2.0.0 as its stable upstream base. See
FEATURE_PARITY.md for implementation status and UPSTREAM_STRATEGY.md for the
maintenance plan.

## Verification

- Full configured host suite: 1,756 tests passed after source formatting.
- The latest experimental X4 Pro build passed before formatting-only changes.
- No post-test58 hardware validation or final independent release review is
  claimed for this snapshot.
- The SDK submodule remains pinned. Its local changes are reproduced by
  scripts/patch_freeink_sdmmc.py and scripts/patch_freeink_volume.py and their
  checked-in patch files.

## Experimental work

Completion journaling and moves, wireless publication, chapter prefetch,
Carousel, statistics restore and backup, chapter-time estimates, and Page
Overlay have incomplete review or hardware validation. Some use local build
flags. Do not enable them for a release merely because this checkpoint compiles.
Read the feature inventory and relevant tests before changing those flags.

Known remaining concerns include real SD power-loss recovery, filesystem
allocation ownership, migration completeness, runtime memory and latency,
visual parity, and interactions among experimental features. The next upstream
integration should reconcile equivalent fixes before adding more local code.

## Checkpoint policy

Chad authorized checked WIP commits and pushes on the personal development
branch. They require a scoped diff review, secret/file scan, and relevant checks.
Releases and merges still require the full firmware handoff and device testing.
