# Series and genre browsing

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

## Index and state

Library index version 3 adds two permutations and series/genre/position fields.
Version 2 remains readable during rebuild reconciliation so firstSeen values
survive the upgrade. Current display opens require version 3. All writes use
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

## Validation status

The review candidate passes 1402 host tests, 181 native ASan/UBSan tests, and
Library/EPUB-loader sanitizer harnesses. Real-font layout checks cover the five
tabs with Library margins in portrait and landscape. An early C3 build passed
before the final content-width layout hook. Independent reviews, final firmware builds, resource
measurements and hardware validation are pending.
