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
