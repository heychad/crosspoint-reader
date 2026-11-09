# Library covers

Settings → Library → Recent books view chooses List or Covers. List is the
default. Covers is available on devices with working PSRAM, including X4 Pro.
Devices without PSRAM retain the list and keep the preference for migration.

The Recent tab keeps its combined order of recently opened books and new
arrivals. The grid uses the same filtered entries and actions as the list.
Settings → Library → Title shelf cover grid independently enables a title-sorted
grid. Author, Series, and Genre retain their list views. Each grid page has
up to six covers, with two-line title labels. A smaller viewport can fit fewer
slots. Labels retain up to 512 UTF-8 bytes.

Tapping a cover opens that book. On Recent, a long press opens book actions.
On other shelves, a long press opens book options, including Reading
statistics and Delete. With no search active, Browse groups is also available.
Delete asks for confirmation. Swiping moves the visible page. If a book is selected, selection stays
on the visible page. Physical navigation selects books, and holding
Next or Previous moves by a grid page. Swipe-to-delete belongs to the list.

## Ownership and resource costs

The Library owns a fallibly allocated `LibraryCoverGrid`, released on exit.
Failure to allocate it uses the list. It reuses the existing `HomeCoverCache`
and SDK cover-grid widget. Six slots have fixed 513-byte title buffers and
128-byte thumbnail-path buffers inside the checked allocation. Book paths are resolved through the Library's existing
scratch buffer and row mapping. The slots retain path fingerprints for cache
identity.

The cover cache uses the existing PSRAM-only allocation contract. Its capacity
is the screen's region size plus seven times the larger screen dimension.
At 480 × 800 this is 53600 bytes. If PSRAM allocation fails, covers draw through
the existing uncached fallback. The Library can be retained underneath child
settings screens, so its cache remains part of their peak working set.

The loop prepares one missing thumbnail per idle pass using the existing
EPUB/TXT/Markdown source-cover helper or XTC thumbnail helper. Parsers use
checked temporary ownership. The Library closes its index while the helper
uses the card, then reopens it. A failed index reopen shows the existing
Library error. Thumbnail failure leaves placeholder art and can be retried
after a page reload or re-entry. Prepared files use existing thumbnail paths.

## Validation status

Production-method harnesses cover real-font grid bounds, action IDs, long
labels, missing covers, parser allocation failure, partial final pages,
filtered/pinned mappings and repeated draws without heap allocations. An early
C3 compilation passed. Independent reviews, final firmware builds, resource
measurements and physical-device checks remain pending.
