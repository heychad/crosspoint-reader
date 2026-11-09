# Reading summary sleep screen

Reading summary is a quieter alternative to the existing Reading statistics
sleep screen. It shows the last open book's title, saved progress, reading time,
and estimated time left. Unknown values show a dash. It reads the same saved
per-book statistics and progress as the existing screen; it does not create a
new statistics format or change the reading position.

If the last book or usable tracked statistics are unavailable, the normal
fallback sleep screen appears. TXT and Markdown progress and estimated time left remain unavailable,
and Quick Resume on Timeout can override the selected screen. The Inverted sleep
cover filter also inverts this screen.

The screen uses the existing temporary statistics object and fixed-size text
scratch, with no new task or persistent data. Its saved sleep setting is value
10; older firmware may reset that choice. Device layout, sleep and wake behavior,
latency, and runtime memory remain unverified on the X4 Pro.
