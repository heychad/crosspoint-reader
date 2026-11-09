# Cover and reading summary sleep screen

This option adds a cached Home thumbnail above the existing reading summary.
It reads the 300-pixel thumbnail first, then the 226-pixel thumbnail. It never
generates a cover on sleep. If neither thumbnail is usable, it shows the text
summary; if saved tracked statistics are unavailable, it uses the same default
sleep screen fallback as Reading summary. TXT and Markdown progress and time
left remain unavailable. Quick Resume on Timeout can override the selection,
and the Inverted sleep cover filter also inverts this screen.

Thumbnail loading allocates one fallible Bitmap object while the existing
statistics object is alive. It accepts images at most 512 by 512 pixels with
one or two bits per pixel and row buffers of at most 256 bytes. The renderer
may use a further bounded row scratch while drawing. Files are closed and the
bitmap released before sleep display. No image or statistics file is written.
The saved mode is value 11; older firmware may reset it. X4 Pro layout, latency,
peak heap, and sleep/wake behavior remain unverified on device.
