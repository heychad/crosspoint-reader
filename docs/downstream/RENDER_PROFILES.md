# EPUB rendering profiles

Choose Default, Balanced, or Light in Settings > Reader or the reader menu.
The in-book choice applies to that book. Reset book settings to return to the
global default. Changing the profile rebuilds the chapter layout and tries
to preserve the current text position.

Default uses the existing CrossPoint rendering. Balanced retains CSS selector matching and stacks table cells. Light also removes publisher layout and
decoration, including margins, borders, and image sizing. Both simpler modes
can look different from the publisher's design. All modes retain contextual CSS selector matching, including rules that hide text.
Use Default when publisher styling matters.

Changing modes invalidates chapter layout caches. The existing reading-position
and clipping mechanisms retain their separate identities; non-default layouts
use different clipping signatures. This does not include automatic low-memory
fallback, CrossInk preference import, or Safe Mode recovery. The extra mode
adds one byte to each preference snapshot and section header, plus parser code.
Device heap, rendering time, and reading-position behavior remain unmeasured.

On the X4 Pro, compare styled tables, images, hidden text, and footnotes in all
three modes. Change modes and reopen the book to check its position, per-book
setting, and reset behavior. Test both reader menus and the orientations used
for reading. Report freezes, reboots, missing text, or layout errors.

## Cached images on the X4 Pro

The first eligible image on a page uses up to 96 KiB of PSRAM for its
packed pixels. Grayscale passes can reuse that copy without rereading it
from the SD card. The reader releases it after drawing the page. If PSRAM
allocation fails, the existing chunked heap cache or SD streaming handles
the image. The EPUB and its on-card image cache keep their existing formats.

The host fixture reads a full-screen 96,004-byte cache file once and
replays it with zero additional reads. All four orientations produce
the same framebuffer as SD streaming, including both grayscale planes.
For a device comparison, go back and forward between cached illustrated
pages with the same orientation, refresh settings, and book layout.
Serial logs identify whether PSRAM was used and report render timing.

When landscape grayscale falls back to SD streaming, each strip reads
only the image rows it draws. In the host fixture, an 80-row strip of an
800 × 480 image reads 16,004 bytes, including the header, compared with
96,004 bytes for a full pass. Portrait strips still need every row.

## Buffered grayscale on the X4 Pro

When the SSD1677 display and PSRAM can stage a full grayscale plane, the
reader draws the page once per plane instead of once per 80-row strip.
A plane uses 48,000 bytes. During an asynchronous black-and-white refresh,
the reader can stage two planes, up to 96,000 bytes total, before waiting for
the panel. Otherwise it reuses one plane. The buffers are released after the
page, and the existing heap or strip paths handle allocation failures.

The host display fixture matches the strip output byte for byte. It confirms
two grayscale draws instead of twelve, the order of refresh and uploads, and
preservation of the black-and-white frame. Use the serial render timings to
check the difference on hardware.

## Chapter indexing

Chapter indexing controls when pages are built, separately from the rendering
profile. Incremental keeps the current default: build enough pages to display
the requested passage, then continue in bounded idle-loop steps. Full chapter
finishes the chapter before displaying it, including a resumed partial cache.
It can make the first display slower but avoids later page-building waits in
that chapter. Percentage jumps still require a complete chapter in either mode.

Choose it in Settings > Reader, the reader menu, or toolbar More. A choice made
with a book open belongs to that book. Reset book settings restores the global
default. Completed chapter caches remain valid because the layout is unchanged.
This control does not enable next-chapter prefetch.

Compatible CrossInk per-book indexing values from versions 4–13 import without
rewriting the source. The existing preference JSON stores `indexingMethod` as
an optional field. Unsupported values remain unmapped in the import preview.
For device validation, use a long uncached chapter, compare the time to its first
page, and confirm the same passage after switching modes and reopening.

Indexing checkpoint validation: all 1,831 host tests passed in 63.21 seconds;
the X4 Pro build passed in 133.87 seconds. Focused tests cover the first-page
policy, completing an existing partial cache, retaining the requested page,
heap admission failure, and picker cancellation. The firmware decoder agrees
with 1,812 reference/truncation/mask cases. Preference snapshots are 80 bytes;
the containing scope remains 168 bytes. Hardware timing remains unmeasured.
