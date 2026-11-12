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
| Sleep transition, `bb63d8f1` | Implemented as a checked WIP. The fork processes the pending transition without another input poll, retains its exclusive-storage guard, and enters the sleep activity synchronously. Regression checks cover both Page Overlay configurations; cumulative review and device checks remain. |
| Clipping anchors, `6ec5bc96` | The fork already retains source ranges across layout changes. A skipped-text accounting defect is fixed in WIP without changing clipping files or reading coordinates; see [clipping anchors](CLIPPING_ANCHORS.md). Generated list/image text now uses unknown source ranges without losing selection. Native table source ranges and clipping versions 1–4 have compatibility checks. Remaining reader and sync differences still need comparison. |
| Carousel spacing, `83ce9237` | Compare against our responsive layout, which already sizes covers and menus together. Its fixed vertical-lift change is not a direct patch for this fork. |
| Custom sleep-image fit/crop, `e36b25b1` | Adapted as WIP using the existing converter. Runtime bounds, native gray levels, content-based cache identity, and checked render fallback are built; see [custom sleep images](CUSTOM_SLEEP_IMAGES.md). Source files and reading state are unchanged; device checks remain. |
| Page Overlay reconstruction and sleep modes | Missing/stale EPUB-family layouts now rebuild through the loaded book with local recovery and unchanged progress/settings; see [reconstruction](SLEEP_PAGE_RECONSTRUCTION.md). RC2 Cover + overlay is now built in the enabled profile. Minimal statistics has an X3-only extra overlay, so sharing Minimal book on X4 Pro matches RC2; see [mode mapping](COVER_OVERLAY_SLEEP.md). Release review and device checks remain. |
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
