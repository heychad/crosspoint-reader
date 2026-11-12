# Clipping source anchors

The fork already stores per-word source ranges and uses them to keep highlights
attached across font, margin, and orientation changes. Clippings without usable
source ranges fall back to page/word positions only when the saved layout
signature matches. Their saved text remains available in Saved Items.

## Hidden text inside words

The RC2 comparison found a remaining source-range defect. Given
`co<span hidden>XYZ</span>operate`, the reader displays `cooperate`, but the old
range ended at source character 9 instead of 12. The parser counted the hidden
characters; the word-range calculation lost them. Similar gaps occur when an
embedded BOM is removed from a word.

The fix records skipped spans and merges them with existing NFC composition
offsets when calculating word ranges. It retains the parser's established
source coordinate, including hidden body text. It does not change reading
progress, sync positions, clipping IDs, or the clipping-file format. Leading
and trailing hidden spans stay outside a visible word's range. Wrap points skip
the hidden span when locating the next visible fragment.

Section cache version 79 rebuilds derived pages with corrected ranges. Both
complete and partial cache versions change together. Existing clipping records
and their text are preserved; this does not rewrite historical clipping ranges.

## Resource use

Skipped spans live in checked, paragraph-owned chunks of eight ranges. Adjacent
spans merge, so a large hidden text block uses one range rather than one entry
per hidden character. Consuming lines releases obsolete ranges; destruction is
iterative. Ordinary text allocates no gap chunks. A failed allocation marks the
layout incomplete and follows the existing readable build-failure path.

Host checks cover hidden text, NFC composition on either side of a gap, Unicode
character counts, a 70,000-character hidden span, line consumption, and a BOM.
The allocation test verifies preservation of existing ranges when growth fails,
adjacent-span merging, and invalid bounds. Cumulative review and device checks
remain for the firmware candidate.

The first cumulative run passed 2,268 of 2,269 checks in 51.94 seconds. The
remaining test hard-coded cache version 78; it now reads the current version
and checks that both previous complete and partial caches are rejected. Its
rerun passed in 0.58 seconds. The default X4 Pro build passed in 79.55 seconds,
with 113,132 bytes of static RAM and 5,657,874 bytes of flash. Each active
`ParsedText` is 436 bytes; a hidden-range chunk is 72 bytes on this target.
The existing local SDK changes are unchanged and excluded from the checkpoint.

Device validation should include the hidden-word, NFD, and BOM examples: save a
clip, change font size and orientation, then compare its text and highlight.

## Generated text and existing clippings

Generated list numbers, bullets, and image fallback labels no longer borrow
book coordinates. They remain selectable. A selection containing generated
text uses the existing page/word fallback, guarded by the saved layout
signature; actual book-only selections retain portable source ranges.
Dictionary selection remains available when its content has no book ranges.
The paragraph boundary stays on the first logical selected word, including a
generated marker, so exporting a list item does not insert a spurious newline.

The flag uses an unused bit in the existing layout-only font-slot byte. Font
lookup masks it, split tokens inherit it, and cached font slots exclude it.
There is no new per-word allocation or serialized field. Generated text still
normalizes accents for display, without registering book-source corrections.
The last buffered image label is flushed before synthetic-text handling ends.

Native table cells already retain real source coordinates. They keep those
ranges; CrossInk's separate compact-table model is not imported. Section cache
version 80 rebuilds derived ranges for list and image labels. The clipping
store stays at version 4. Versions 1–4 are checked for text and coordinate
preservation through a native save and reload; no historical ranges are
invented or rewritten. Records without a usable layout signature remain
viewable without an unsafe page-local highlight.

Cached-page tests cover generated ordered/unordered markers, every image
label token, real table cells, NFC, small caps, font slots, and split tokens.
Selection checks cover export and dictionary lookup with mixed known/unknown
ranges. Cumulative release review and X4 Pro checks remain.

Continue comparing the remaining RC2 reader and sync changes separately;
retain the fork's existing source coordinates and checked stores.

The cumulative host run passed 2,274 of 2,275 checks in 121.48 seconds. The
remaining new fixture assumed its test stub would split explicit hyphens;
it now uses real CJK token boundaries instead. That check and strengthened
version-rewrite checks passed in a three-check rerun (0.10 seconds). Logs are
`/tmp/x4-synthetic-all-tests.log` and `/tmp/x4-synthetic-compat-rerun.log`.
An initial command explicitly selected the inherited `default` C3 environment
and took 340.33 seconds. That is rework, not X4 Pro verification. The build rule
now specifies `pio run -e x4pro` and requires checking the Processing line.
The explicit `x4pro` build passed in 71.48 seconds; its log is
`/tmp/x4-synthetic-x4pro-pio.log`.
This is a WIP checkpoint, not a device candidate. The pre-existing local SDK
edits remain excluded.

For device verification, select a list marker with its text and an image's
fallback label. Confirm saved text and reopening, then change the layout:
book-only clips should retain portable highlights; generated-text clips should
use the existing matching-layout fallback. Check dictionary selection as well.
