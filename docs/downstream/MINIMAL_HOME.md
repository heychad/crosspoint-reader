# Minimal Home

The Minimal Home theme adds a centered cover, title, and author above the
existing Home menu. It uses the current theme menu and the existing navigation,
cover loading, and thumbnail paths. This first layout does not include all of
CrossInk's Minimal theme or the Dashboard theme.

Minimal Home uses saved theme value 5, after the existing values 0–4. It is
shown when PSRAM is available, like the adjacent Cover Grid choice. Older
firmware does not know value 5 and may reset the theme on downgrade.

The Home activity owns the existing cover-region buffer and frees it on exit.
The taller 390-pixel tile may need roughly 23 KiB for a 480-pixel-wide
monochrome region before byte alignment, compared with roughly 15 KiB for the
242-pixel Lyra tile. These are estimates, not runtime measurements. If buffer
allocation or restoration fails, the cover is drawn again. A failed thumbnail
generation keeps the shared saved cover path and displays a placeholder until
Home is reopened. No task or network
connection is added. Cover decoding, full peak heap, and device behavior remain
to be measured on the X4 Pro.
