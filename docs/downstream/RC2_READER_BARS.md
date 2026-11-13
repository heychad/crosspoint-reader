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
retain input ownership. The later XTC stage below adds RC2's fourth placement mode.

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

## Custom-bar text size

Reader bar layouts now offers Bar text size with Small, Medium, and Large.
The choice applies to both custom bars. Small preserves their existing font
and lane height. Medium and Large use the already registered UI 10 and UI 12
font families. The lane grows to at least the chosen line height plus four
pixels, keeping text inside its reserved space. Clipping, separators, titles,
and battery percentages use that font consistently.

Changing the size through Reader bar layouts uses the existing content-offset
handoff and layout rebuild. Native bar JSON keeps its current format; the
new global `statusBarTextSize` value is an integer from 0 through 2 in the
existing settings catalog. Preferences import maps the same RC2 values with
strict validation and native ownership. Source files and the exact settings
snapshot retain the existing protection.

The native legacy bar path keeps its existing font. The XTC stage below applies the same custom fonts to bitmap overlays and
handles the legacy top-uses-bottom layout. This stage
adds no font files, font registry, task, or retained rendering buffer. Battery
drawing accepts an optional font ID; existing callers retain the small UI font.
The four-row root uses the existing fourteen-row storage.

Geometry checks cover all sizes, both positions, four rotated inset sets,
centered/un-centered layouts, and narrow/portrait/landscape widths, including
long text, progress-only bars, transient text, and selected-font battery calls.
Size selection rejects invalid results. The production preferences transaction
checks the published size and exact source/backup preservation.

The default X4 Pro build passed in 130.85 seconds, using 113,140 static RAM
bytes and 5,679,894 flash bytes. The bar-settings activity is 2,720 bytes;
CrossPointSettings is 752 bytes, excluding transaction documents and buffers.
No additional font allocation is introduced: all three families were already
registered at startup.

The full host run passed 2,359 of 2,360 checks in 108.89 seconds. The added
geometry test assumed a thinner progress line than its fixture configured.
After correcting that fixture, 91 affected checks passed in 2.65 seconds.
The expanded orientation/position matrix also passes. Geometry and imports
pass AddressSanitizer and UndefinedBehaviorSanitizer. Logs use
`/tmp/x4-bar-size-*.log`. Firmware inputs were unchanged by the fixture-only
correction. Cumulative review and X4 Pro rendering/reflow checks remain.

## XTC placements and compatibility

XTC bars now offer Hide, Bottom, Top, and Both. Existing numeric values 0–2
stay unchanged; Both is 3. Select placement in Reader bar layouts or the legacy
status-bar settings. With custom bars enabled, each displayed position uses
its configured layout, including text size, title, chapter page/percentage,
progress, battery, clock/date, and available reading-time estimates. With custom
bars disabled, the existing native footer is used at the selected positions.

Older native Top saves without a compatibility field keep using bottom-layout
content at the top. The native `readerBarsV1` object now saves
`legacyXtcTopUsesBottom` as a boolean. The displayed top bar's own hidden state
still wins. Changing placement on the device or editing top-bar content/style clears the
compatibility flag; choosing the same value or only hiding the top bar retains
it. Older firmware can ignore the new boolean. It cannot display Both and may
reset that unsupported mode when saving; book progress is not part of this
settings change.

The explicit bars import carries RC2's nested `xtcMode` and compatibility flag.
An absent nested mode falls back to the older global `xtcStatusBarMode`; a
present malformed nested value rejects the import. Flat legacy Top saves use
the bottom layout. Layouts, placement, and compatibility are staged together
before the existing backed-up settings transaction writes. Original source
files and reading-state files remain unchanged by import.

Both 1-bit and 2-bit page paths now apply the overlays. The grayscale base and
cleanup frames draw bar content; each grayscale mask pass clears the selected
bitmap strips before its mask is copied. Persistent hidden bars and temporary
hiding still clear those strips, keeping the bitmap's original status content
from showing through. Hide placement leaves the original bitmap intact.
Buffer-only sleep reconstruction applies the same base overlay without driving
the display or advancing the refresh counter.

The port reuses the existing page buffer, renderer, settings writer, and stats
estimates. It adds no task or image-sized buffer. The overlay owns one small
bar-config copy, two 32-byte time labels, and conditional title strings during
drawing. These are released between passes. Runtime heap/stack headroom and
physical grayscale output still need X4 Pro validation.

All 2,364 host checks pass in 96.24 seconds. Overlay rendering, native compatibility
policy, both 5,832-case legacy import matrices, and the actual padded 1/2-bit
render/sleep path pass AddressSanitizer and UndefinedBehaviorSanitizer. The
policy target includes 70 cases. Import publication preserves the exact native
snapshot and unrelated settings. Same-value top selections retain compatibility.

The default X4 Pro build passes in 102.62 seconds at 113,148 static RAM bytes
and 5,681,958 flash bytes. CrossPointSettings is 756 bytes; XtcReaderActivity
remains 928, and bar settings remains 2,720. The overlay function's S3 frame is
272 bytes, excluding callees. Parsed metadata titles are bounded at 127 bytes
and chapter names at 80; filename fallback uses the existing XTC/filesystem
contract. No image buffer is added. Logs use `/tmp/x4-xtc-bars-*.log`.
Cumulative review, runtime headroom, and physical grayscale/strip results remain
pending. No firmware was installed.
