# Carousel home

The experimental Carousel shows up to three recent books, with a selected
center cover, neighboring covers, reading time, progress, and a horizontal
menu. Swiping the cover area selects the next or previous book. Tapping a side
cover selects it; tapping the selected center cover opens it. Swipes elsewhere
do not become a synthetic Back action.

The cover area now shrinks to leave space for the menu and button hints in
landscape. Cover proportions stay at 3:5, and the menu reserves enough height
for its selected label. Rendering, cover taps, menu taps, and cover swipes use
the same dimensions. Other home themes retain their configured cover height.

This changes geometry, not book paths, reading positions, cache formats, or
statistics. It reuses the existing cover snapshot and does not add a buffer,
allocation, task, or persistent setting. The snapshot covers the smaller
landscape area; runtime heap and latency still need device measurement.

Host checks exercise production cover and menu drawing at 480×800 and 800×480,
with 16- and 32-pixel label lines, zero through three books, and book/menu
selection. Separate checks cover the footer and matching hit targets. The full
Home input method is checked with Carousel enabled, disabled, and with deferred
completion enabled. These checks do not prove visual parity on e-ink.

Next device check: select Carousel, swipe and tap between three books, open
each menu icon, then check the same layout after rotating the screen. Confirm
that title, reading time, progress, menu label, and button hints stay separate.
Visual treatment and device input acceptance remain open before release.

Validation: 46 of 47 affected checks passed on the first run. The Home input
fixture needed the new sizing helper; its corrected rerun passed in 0.52 s,
as did direct runs with Carousel and deferred completion enabled. The
`x4pro-carousel-wip` firmware build passed in 190.30 s, including first-use
dependency setup. It used 113,036 bytes of static RAM and 5,655,454 bytes of
application flash. Runtime heap use and physical display behavior are unverified.
