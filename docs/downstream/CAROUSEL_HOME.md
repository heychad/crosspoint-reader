# Carousel home

The experimental Carousel shows three recent books, with a selected center
cover, angled neighboring covers, reading time, progress, and a horizontal
menu. The RC2 visual treatment is adapted to the fork's responsive geometry:
rounded center art, a stronger selected border, a bold two-line title, and a
filled menu highlight with a white icon. The selected menu label is centered
above the icons, so it can use the available width.

Swiping the cover area selects the next or previous book. Tapping a side cover
selects it; tapping the selected center cover opens it. Left/Right buttons cycle
within the active cover or menu row. Up/Down switches rows and returns to the
last highlighted cover. Swipes elsewhere are consumed, without opening a menu
item or producing a synthetic Back action. The existing Confirm, Back/resume,
button mapping, and touch haptic paths remain in use.

The cover area shrinks in landscape to leave space for reading information,
menu labels, and button hints. Center covers retain 3:5 proportions. Side-cover
slots sit next to the center cover rather than stretching to the screen edges.
Rendering and hit testing share the same geometry. Other themes retain their
configured cover height. This adapts RC2's fixed vertical-lift fix rather than
copying its portrait dimensions.

## Resources and recovery

The renderer projects decoded BMP rows directly into the existing framebuffer.
It uses one checked row allocation, released before the next cover. The parsed
BMP limits bound it to 8,704 bytes for a 2,048-pixel 32-bit source; normal 1-bit
thumbnails need less. This replaces the prior side-cover bitmap row allocation,
without retaining an additional image or frame buffer. A partial decode or
allocation failure clears the slot before drawing a silhouette. Center-cover
failures use the existing placeholder.

The existing cover-region snapshot is reused. Menu selection restores cached
artwork without re-reading covers; a failed snapshot causes a redraw. There is
no new setting, persistent file, worker, or network owner. Book paths, reading
positions, statistics, and cache formats are unchanged. Runtime heap, power,
latency, and appearance still require X4 Pro measurements.

## Verification

Host checks exercise production drawing at 480×800 and 800×480, with 16- and
32-pixel label lines, zero through three books, and missing, malformed, or failed
cover reads. Cache restore and failed snapshot retry are checked separately.
The projection test checks individual pixels, both BMP row orders, up/down
scaling, screen clipping, legacy and absolute gray planes, dark-mode image
polarity, allocation failure, I/O failure, and black/white icon drawing under
AddressSanitizer and UndefinedBehaviorSanitizer. The complete Home input method
is checked with Carousel disabled, enabled, and combined with deferred completion.

All 2,284 host checks passed in 64.90 seconds. The two strengthened projection
and draw-failure tests passed again; the affected CTest rerun passed 3/3 in
2.05 seconds. The final Home branch cleanup passed with Carousel disabled,
enabled, and combined with deferred completion. The final standard X4 Pro build
passed in 66.05 seconds (113,132 bytes static RAM; 5,658,482 bytes application
flash). The enabled Carousel build passed in 65.69 seconds (113,044 bytes static
RAM; 5,663,938 bytes flash, leaving 889,662 bytes in its application slot).

The first build pair took 141.12/122.72 seconds because the renderer header
change rebuilt callers. The second pair followed the small input-branch cleanup;
no build is attributed to documentation-only changes. This is core implementation
evidence; cumulative review and device acceptance remain open.

Next device check: select Carousel, swipe and tap between three books, then
navigate its covers and menu using buttons. Rotate through all four orientations.
Check that title, time, progress, menu label, and button hints stay separate, and
that menu gestures do not open a book. Include a book without a cover and reopen
Home after reading. No installation is part of this checkpoint.

Source: CrossInk [v2.0.0-rc2](https://github.com/uxjulia/CrossInk/tree/v2.0.0-rc2),
including its Carousel artwork, controls, and spacing change `83ce9237`.
