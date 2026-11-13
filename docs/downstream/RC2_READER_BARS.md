# RC2 reader-bar controls

The X4 Pro fork now has independent hidden state, centered items, separators,
and battery styles for each reader bar, plus chapter percentage as a slot.
These are checked WIP features; release review and device acceptance remain.

## Controls and drawing

Open Reader bar layouts from the reader menu, then choose Top or Bottom.
The seven slots, progress mode/thickness, and percentage precision retain their
existing controls. Four additional rows control visibility, centering, battery
display, and separator. Hidden bars remain editable but reserve no reading
space and draw nothing, including transient titles. Leaving the settings screen
uses the existing source-offset-preserving repagination path.

Centered mode draws nonempty slots as one group in slot order. Bullet, dash,
pipe, and middle dot separators are available. Overflow trims wide items;
extremely narrow lanes drop trailing items so separators stay inside the lane.
Bookmark space stays reserved. The existing font and glyph renderer are used.

Battery display offers the general setting, icon with percentage, icon only,
or percentage only. Explicit choices override the general battery-percentage
setting for that bar. Existing native layouts retain the general setting.
Chapter percentage uses the current displayed page divided by the EPUB section
page count, with the bar's configured precision; an unavailable count displays a
dash. A book can split a chapter across several sections. The value follows
this fork's existing chapter progress bar.
It does not alter book progress or reading statistics.

The top bar moves up one additional pixel, clamped at the display edge. Progress
still fills from the screen edge and leaves the unfilled portion invisible.

## Settings and import

Native bar JSON adds `hidden`, `centered`, `batteryStyle`, and `separator`.
Absent fields keep the old behavior. Invalid types/ranges reject the bar without
partially changing the decoded result. Chapter percentage uses a
`chapterPercentSlots` mask over the existing BookPercent slot ID; base slot IDs
remain 0–9 in every build. Chapter-time masks retain their existing mechanism.
An older parser can still read the base slots, displaying book percentage for
the new slot. Older firmware can discard the new extensions when saving.

The explicit CrossInk import maps RC2's chapter-percentage slot and per-bar
styles by meaning. Missing source battery styles use the legacy percentage
visibility setting, or CrossInk's icon-and-percentage default. Flat imports
use the same fallback. Malformed styles keep the current layout unchanged.
Source files, settings snapshots, and the existing write/recovery protocol are
preserved. No book-cache or reading-state format changes.

## Resource and verification evidence

There is no new heap buffer, worker, or render cache. `ReaderBar` grows from
10 to 14 bytes; the two global bars add eight bytes. The centered layout uses
the existing local geometry object, now 64 bytes. The standard ELF's renderer
frame is 224 bytes, up from 208; this excludes callees and is not a device
stack-headroom measurement. The bar-settings activity is 2,720 bytes, including
14 rows of 60 bytes each; four added rows account for 240 bytes. It uses the
existing checked activity/picker allocation and popup ownership. The battery
popup can retain four labels instead of three, plus their string storage.

The full 2,290-test suite passed in 73.26 seconds. Twelve bar checks then passed
in 0.09 seconds after adding a malformed-style test. Recording-renderer checks
also passed with AddressSanitizer/UndefinedBehaviorSanitizer, with and without
chapter time. Tests cover centered bounds and bookmark space, hidden lanes,
per-bar battery choices, chapter percentage and missing counts, JSON fallback,
48 RC2 style combinations, malformed fields, and native control callbacks.

Both X4 Pro builds passed: standard in 128.61 seconds (113,140 static RAM bytes;
5,668,454 flash bytes), and the combined eight-feature profile in 109.84 seconds
(113,068 static RAM bytes; 5,750,718 flash bytes). The combined profile enables
completion/prompt/move, statistics restore, daily statistics, Page Overlay,
Carousel, and chapter time. Runtime heap, physical glyph appearance, latency,
and all-orientation device acceptance remain unmeasured.

For the next device smoke test, hide and restore the top bar, enable centering,
change the separator and battery display, then select chapter percentage.
Confirm the expected items and no overlap in both portrait and landscape.
Turn pages, create/cancel a clipping, then sleep/wake and check reading position.
Run the remaining orientations during the release regression.

## Temporary tap-to-hide

Enable Settings → Controls → Tap to hide reader bars. A tap in an enabled top
or bottom bar's 64-pixel edge strip hides both bars. Tap either enabled strip
again to restore them. The hit targets remain available while hidden. EPUB
keeps its reserved bar space and page layout; XTC redraws the same page without
the overlay. Persistent hidden bars have no tap target. Disabling this option
restores the bars, and opening a new reader starts visible.

The option defaults off for existing native settings. Import CrossInk
preferences carries the validated 0/1 source value. The toggle uses the existing
settings catalog and writer. Temporary visibility is a reader-owned atomic
flag; it is not written to settings or reading state. There is no new heap
buffer or worker. Existing menu/selection overlays and the global input lock
retain input ownership. This does not add RC2's fourth XTC placement mode.

The controls remain available when page-turn gestures are disabled, provided
reader touch is enabled. Inside an enabled edge strip, the bar interaction takes
priority over page taps and links. Outside it, normal reader actions apply.

Production tap handling passes host checks across four logical orientations,
viewable margins, edge boundaries, disabled reader touch, missing hardware touch,
disabled bars, hiding/restoration, and XTC menu input. Tap routing and preference
imports pass AddressSanitizer and UndefinedBehaviorSanitizer; imports cover
default, backup, and Page Overlay configurations.

The full host run passed 2,359 of 2,360 checks in 110.46 seconds. Its one failure
was the XTC menu fixture missing the new bar-height dependency. After updating
the fixture, 32 affected checks passed in 3.18 seconds, including both XTC menu
configurations. Firmware source was unchanged by that correction. The default
X4 Pro build passed in 135.32 seconds at 113,140 static RAM bytes and 5,678,794
flash bytes. Compiled object sizes are ReaderActivity 912, EpubReaderActivity
3,016, and XtcReaderActivity 928 bytes, excluding their owned allocations.
Logs use `/tmp/x4-bar-tap-*.log`. Cumulative review, runtime headroom, and physical
render/input behavior remain unverified; no installation was performed.
