# Next-chapter prefetch draft

This uninstalled draft is intended to prepare a next section while idle on the last two pages
of a completed section. The last device-tested firmware was test2; test48 does
not include prefetch. Prefetch is disabled in this draft pending review fixes.
The draft uses the existing loop and render lock and does not change reading
position. It also builds on experimental sync and storage changes that require
cumulative and device validation.

The current prototype handles EPUBs with already-cached HTML of at most 8 KiB.
It skips protected books, Bluetooth page turners, image chapters, sections over
32 pages or 64 anchors, and low-memory situations. It does not yet prefetch
uncached chapters. A parse step aims for 12 ms between checks; setup, storage,
and individual parsing operations can take longer. Device speed, power use,
memory, and responsiveness are unmeasured.

Speculative output is created exclusively as a separate `.prefetch` file and
closed between steps. Contents are synced before its final version marker,
avoiding the rename that can leave FAT aliases. The reader does not yet load
these files: cache validation, recovery, and sync mapping need integration.
A process-local storage marker rejects
continuation after an intervening HAL mutation; it does not detect raw writes
outside the HAL.

Test2 and stock rollback loader code ignore the separate filename in host tests.
Real reader recovery, complete page loading, and physical rollback remain
untested. Page and section checks do not yet bound nested decoders to each
page, so a corrupt cache can still cross record boundaries or request large
allocations. This design needs bounded decoding, review, bounded setup, and
uncached prefetch before the feature can be enabled. Cached HTML is rebuildable
and is separate from reading positions and statistics.
