# Library metadata and author ordering

The chad.35 work adds Series and Genre tabs to the existing Library. Ascending
and descending orders, search, filters, row actions and group collapse share the
existing Library model. Content-width tabs keep the five labels readable at
the tested portrait and landscape widths. Unknown values form No series or
Uncategorized groups.

Series metadata uses the existing EPUB parser's Calibre and EPUB 3 collection
support. The first nonempty subject becomes the genre, following CrossInk's
policy. Text is whitespace-normalized and bounded. The Library keeps at most
128 UTF-8 bytes for each series and genre field. Numeric series positions sort
within a series, followed by title order. Ascending order places unknown
positions last. Descending order reverses the entire order, including unknown
positions.

Show series and Show genre control row subtitles. Both default on and use the
normal global settings serializer and web settings route. CrossInk settings
import is not included in this candidate. They affect the displayed details. The Series and Genre tabs remain
available. EPUB metadata extraction follows the existing Library metadata
setting.

## First-name index order

The current index also stores ascending and descending author first-name orders.
They compare the folded canonical display name, including long shared prefixes,
rather than the surname key or its 12-byte prefix. Unknown names sort last in
ascending order; descending reverses the whole order. Equal names retain title
order. Both author orders use the same spelling chosen by the existing author
vote. The first-name order is a prerequisite for the next Library view update;
it is not exposed by the current five-tab UI yet.

This adds one bounded sort pass through the existing sorter. Its 32 KiB maximum
run buffer, 4 KiB fallback, temporary run file, and per-pass text scratch retain
their existing ownership. The pass uses the existing canonical-author map and
releases its scratch before the name blob is written. It adds no resident
per-book array. At the 4096-book ceiling, the extra on-card permutation costs
8 KiB including section alignment.

## Index and state

Library index version 3 added series/genre/position fields and their two
permutations. Version 4 appends the first-name permutation, with the same
128-byte record and name-blob layout. Versions 2 and 3 remain readable during
rebuild reconciliation so firstSeen values survive the upgrade. Version 3 rich
metadata remains readable during reconciliation. Current display opens require
version 4. All writes use
the existing staged index installation. A failed rebuild preserves the prior
index. Book paths and reader caches are outside this index migration.

The existing title, author and arrival sort implementations retain their
semantics. Series and genre use the bounded sorter adapted from the pinned
CrossInk source. Its buffer is at most 32 KiB and can shrink to 4 KiB, spilling
sorted runs to a temporary card file. Refinement of long shared prefixes uses
a checked heap stack.
Author-sort and arrival scratch allocations are released before metadata
sorting. The Library reuses its per-page author strings for composed subtitles. Filename,
series, genre and group-comparison scratch belongs to the activity, is reserved
on entry and is released on exit.

Reader book-cache formats retain their existing versions. The initial rich-metadata pass reads
the source package because existing reader caches contain only basic metadata.
If the source metadata is unavailable, the basic title/author cache remains a
fallback. Later builds can reuse the rich index fields.

## Current validation

The full 2353-check host suite passed in 92.69 seconds. The native builder and
sorter passed all 37 cases under AddressSanitizer and UndefinedBehaviorSanitizer,
including the 4096-book ceiling. New checks cover distinct first/surname order,
long shared prefixes, accented names, canonical spellings, and spilled runs.
Both older layouts migrate across changed section offsets, preserve arrivals,
and retain the old index after an injected rebuild-write failure.

The default X4 Pro build passed in 85.32 seconds, with 113140 static RAM bytes
and 5674622 flash bytes. Logs are `/tmp/x4-first-name-full-checks.log`,
`/tmp/x4-first-name-sanitizers.log`, and `/tmp/x4-first-name-firmware.log`.
Runtime resource measurements, cumulative handoff review, and device validation
remain.

## Initial series and genre validation

The review candidate passes 1402 host tests, 181 native ASan/UBSan tests, and
Library/EPUB-loader sanitizer harnesses. Real-font layout checks cover the five
tabs with Library margins in portrait and landscape. An early C3 build passed
before the final content-width layout hook. Independent reviews, final firmware builds, resource
measurements and hardware validation are pending.
