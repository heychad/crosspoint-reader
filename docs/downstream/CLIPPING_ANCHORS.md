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

## Remaining RC2 comparison

This closes the demonstrated skipped-text accounting defect. Continue comparing
synthetic list/table text, split-word identity, and old clipping compatibility
with CrossInk `6ec5bc96`; do not import its whole parser or storage format merely
because the file names overlap. The fork's existing source-coordinate handling
and checked allocations must be retained.
