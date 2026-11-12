# Custom sleep images

This WIP adapts CrossInk RC2's `e36b25b1` sleep-image fixes to the fork's
existing BMP converter and grayscale API. Source images, book paths, reading
positions, and settings formats are unchanged.

## Behavior

- Fit and crop use the renderer's current bounds, including 480×800 and
  800×480. Crop fills the bounds; fit preserves the aspect ratio and centers
  the resulting bitmap through the existing placement helper.
- Resizing retains native gray levels. Other color images use the existing
  two-bit dithering; one-bit sources keep their one-bit refresh path.
- Converted-cache keys include a content fingerprint, dimensions, fit/crop,
  threshold choice, and a `v2` suffix. Replacing an image with the same byte
  count and FAT timestamp now refreshes the cache. Already screen-sized,
  one/two-bit sources are read directly.
- Failed conversions, cache reads, and bitmap drawing try the existing direct
  source or default fallback. Cache files close before removal, and the source
  closes before a fallback reopens its path. Failed temporary outputs are removed.
- Bitmap drawing now returns failure for failed B/W drawing, missing retained
  frames, failed absolute grayscale bases, and incomplete gray planes. The
  older refresh-mode base API returns no result; its contract is unchanged.

Derived images remain at `/.crosspoint/sleep_<hash>.bmp`. The new key leaves old
converted images unused; this stage does not add cache garbage collection or
change source images. Each X4 Pro two-bit full-screen cache is at most 96,070
bytes. Development artifacts staged manually on the SD card belong in `/zDev/`.

## Resource use

The fingerprint uses a 128-byte stack chunk, rewinds the source, and yields
after every 64 KiB. It reads the complete source when conversion caching is
needed; sleep latency on a real SD card still needs measurement.

Conversion reuses the existing checked row buffers, not another framebuffer.
For X4 Pro width up to 800 and the parser's 2,048-pixel input-width limit, the
worst-case row payload is 20,512 heap bytes: 8,192 raw, 4,096 gray, 3,200 sums,
200 packed output, and 4,824 dither errors, plus the small owner objects. The
largest block is 8,192 bytes. These are calculated bounds, not runtime heap
measurements. Buffers die on return; native gray sources skip the two-bit
dither allocation. No task, network work, or persistent setting is added.

## Verification

The full configured host suite passed 2,281 checks in 53.18 seconds. After a
firmware compile exposed a mismatch in the legacy grayscale test stub, the
source call and typed stub were corrected. All 22 affected checks then passed
in 3.46 seconds. The final standard `x4pro` build passed in 109.36 seconds,
using 113,132 bytes of static RAM and 5,658,362 bytes of application flash.
The `x4pro-completion-wip` profile, including Page Overlay, passed in
119.98 seconds. A later harness correction replaced its fake frame-retention
helper with the production helper and passed in a direct rerun. Pre-existing
SDK edits are unchanged and excluded from the checkpoint.

Checks exercise actual BMP conversion in both orientations, top/bottom row
orders, fit/crop, native gray levels, invalid bounds, truncated input, and short
writes. The cache/render harness executes the production methods with checked
file ownership and ASan/UBSan. It covers same-size replacements, cache reuse,
failed conversion/close/rename, invalid caches, direct fallback, failed gray
planes, and restoring B/W render mode. Logs are under
`/tmp/x4-sleep-images-*` and `/tmp/x4-sleep-image-cache.log`.

Cumulative release review and X4 Pro checks remain. At the next reviewed device
handoff, compare fit/crop with an existing small and large sleep image, replace
an image without changing its size, and confirm the new image appears. Check
grayscale/inversion, transparent-overlay fallback, sleep latency, and wake/resume.
