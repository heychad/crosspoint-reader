# Dashboard Home

Dashboard Home shows the most recent book's cover, title, author, saved progress,
reading time, and estimated time left above the existing Home menu. It is an
initial dashboard, not complete CrossInk theme parity. Missing progress or
unavailable, disabled, or unreadable statistics display as a dash. Progress and
time left are estimates. Progress and time left are unavailable for TXT and
Markdown, avoiding cache changes during Home entry. The theme is offered when
PSRAM is available.

The Home activity loads data once per entry and owns a small summary until exit.
Per-book statistics use read-only loading and respect tracking settings. The
progress reader and statistics lookup can perform SD reads before the first Home
render; latency, loop-task stack, and runtime heap remain unmeasured. A temporary stats object is
released after loading. The existing Home activity owns the cover buffer, which
can use approximately 23 KiB at 480 pixels wide before alignment; allocation
failure falls back to redrawing. No task or network connection is added.

Dashboard Home uses saved theme value 6. Older firmware can reset that selection.
The X4 Pro needs physical checks for the visual layout, saved progress, tracking
settings, first-entry latency, and internal/PSRAM headroom. Measure loop-task
stack high-water with instrumentation if available, using a large EPUB and
missing or legacy-only statistics.

The shared progress reader also serves Cover Grid and the statistics sleep
screen. Unsupported record lengths and XTC page numbers beyond the current
book length now show as unavailable there too. No saved position is rewritten
by this check.
