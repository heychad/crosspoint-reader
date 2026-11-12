# CrossInk RC2 review

Checked November 12, 2026. CrossPoint's latest stable release remains 2.0.0.
CrossInk released [v2.0.0-rc2](https://github.com/uxjulia/CrossInk/releases/tag/v2.0.0-rc2)
at 05:16 UTC on November 12. Its tag resolves to
`959e3d1575cf1cf49475c4c4546e49adadf4d477`.

RC2 adds 48 commits after the current CrossInk reference, touching 365 files.
This is an initial review, not a claim that the release has been integrated.
The fork still uses CrossPoint 2.0.0 and its pinned SDK. The local SD-card SDK
changes remain separate and uncommitted.

## Integration order

| Change | Fork assessment and next action |
| --- | --- |
| Sleep transition, `bb63d8f1` | Next fix. The fork's `goToSleep()` calls `loop()`, which polls the outgoing reader before processing its pending replacement. Process the transition without another input poll. Preserve the native exclusive-storage guard and synchronous sleep rendering. |
| Clipping anchors, `6ec5bc96` | Review before another clipping release. RC2 adds stable source-word identities across layout changes and changes parser, page, clipping, and sync formats. Compare with our existing layout-signature guard; do not copy the new storage format without migration and preservation tests. |
| Carousel spacing, `83ce9237` | Compare against our responsive layout, which already sizes covers and menus together. Its fixed vertical-lift change is not a direct patch for this fork. |
| Custom sleep-image fit/crop, `e36b25b1` | Review in the E2 batch. It adds BMP scaling and changes sleep rendering; existing transparent-overlay and grayscale behavior must remain covered. |
| Statistics across firmware switches, `6f0eb316`, `0ddaa267`, `c551ceb9` | Compare with our checked stores, reset behavior, and device identity. Retain existing reading data; do not replace the storage model wholesale. |
| Single-pass antialiasing, `bbb528a0` | Queue for the planned performance pass. It depends on a different SDK pin. Measure the X4 Pro benefit before updating the SDK. |
| Tilt sensitivity, `dde251c3` | Review as an addition to the existing X4 Pro controls after the correctness fixes. |
| New boards and language packaging | Other board targets remain outside this fork's scope. Preserve current CrossPoint language and plugin capabilities. |

The sleep-image polarity change is partly covered already: the fork preserves
Quick Resume and displayed Page Overlay colors, and clears inversion on its
unavailable-layout fallback. Compare remaining overlay paths during E2 work.

## Source policy

Use the RC2 tag for the new comparison. Keep the earlier RC audit as historical
evidence; do not replace its references or label all RC2 changes complete.
Adapt relevant fixes in bounded commits with their original human authors
credited. Candidate builds still require cumulative review and a short device
handoff. No firmware is installed by this review.
