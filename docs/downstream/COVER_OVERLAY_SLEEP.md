# Cover and overlay sleep mode

The experimental Page Overlay profile adds **Cover + overlay**, native mode
15. It draws the last book's cover beneath the chosen BMP/PNG artwork. The
existing CrossPoint transparent-cover mode remains mode 8 and keeps its
separate `/sleep-overlay.bmp`, `/sleep-overlay.png`, and overlay directories.

## Selection and fallback

Cover + overlay uses the same favorite, preferred folder, normal sleep folders,
and root BMP/PNG locations as Page Overlay. Favorite controls in the image
viewer support both modes. Each image attempt starts from the plain cover,
so a partial decoder failure cannot contaminate the next attempt. Failed art
can fall through to another selected source; absent or unusable art falls back
to the plain cover. If neither cover nor artwork is available, the default
sleep screen is shown. A valid overlay can still appear on a blank background
when the book has no usable cover.

This mode always paints in portrait, including entry from a rotated reader or
Home screen. Timeout Quick Resume keeps its existing priority. It uses the
existing cover conversion, grayscale, inversion, and alpha-overlay paths.
No extra framebuffer, decoder, task, or retained allocation is added. Redrawing
the cover uses the existing checked bitmap row scratch, released before the
overlay decoder runs. Runtime heap and latency checks remain for the device.

Mode 15 is offered only with `CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY`. It appends
to the native enum without renumbering existing choices. Older or non-enabled
firmware may reset this choice; the reading-state backup remains part of the
normal device handoff. CrossInk preferences import mode 12 as native mode 15
only when the feature is enabled. Disabled builds preserve the current choice.

## X4 Pro sleep-mode mapping

CrossInk RC2 is the reference for this table.

| CrossInk value | CrossInk mode | Native counterpart |
| --- | --- | --- |
| 0 | Dark | 0 Dark |
| 1 | Light | 1 Light |
| 2 | Custom | 2 Custom |
| 3 | Cover | 3 Cover |
| 4 | Blank | 5 Blank |
| 5 | Cover custom | 4 Cover custom |
| 6 | Page overlay | 14 Page overlay, enabled profile |
| 7 | Reading statistics | 9 Reading statistics |
| 8 | Minimal | 12 Minimal book |
| 9 | Quick Resume | 6 Quick Resume |
| 10 | Minimal statistics | 12 Minimal book on X4 Pro |
| 11 | Dashboard | 13 Book dashboard |
| 12 | Cover overlay | 15 Cover + overlay, enabled profile |

Minimal statistics is not a separate X4 Pro layout in RC2. Its extra global
statistics overlay is guarded by `gpio.deviceIsX3()` in
[MinimalTheme.cpp](https://github.com/uxjulia/CrossInk/blob/v2.0.0-rc2/src/components/themes/minimal/MinimalTheme.cpp#L770).
Its cover geometry uses that same condition. Keeping values 8 and 10 mapped to
Minimal book preserves the reference's X4 Pro behavior rather than adding an
identical menu choice. This corrects the earlier inventory's claimed gap.

## Verification

Checks execute the production mode enum, Settings labels, sleep entry, cover
routing, favorite controls, selection/fallback, and bitmap dispatch. They cover
both feature-flag states, all four reader orientations, entry from Home, EPUB,
TXT, Markdown, XTC/XTCH, missing/invalid covers, failed artwork, redraw before
retry, plain-cover fallback, and Quick Resume. Selected harnesses use ASan/UBSan.
All 13 CrossInk mode mappings are checked with enabled and disabled imports.

At the next reviewed device handoff, select Cover + overlay, set an existing
favorite, and sleep from a book and Home. Check the cover beneath white/alpha
regions, both filter polarities, and wake/resume. Then clear the favorite and
check the folder selection. Cumulative review and device acceptance remain;
this WIP is not an installation candidate. Stage development artifacts under
`/zDev/`.

The 18 affected checks passed in 3.35 seconds, and the full configured host
suite passed 2,283 checks in 49.00 seconds. An added missing-cover/no-artwork
check exposed a blank-screen fallback; after fixing it, all 18 affected checks
passed in 2.64 seconds. The standard `x4pro` build passed in 115.73 seconds
(113,132 static RAM bytes, 5,658,506 application-flash bytes). The final enabled
`x4pro-completion-wip` build passed in 71.36 seconds (113,060 static RAM bytes,
5,713,486 application-flash bytes). The final correction is inside the
Page Overlay gate and does not change the standard build's path.

Logs: `/tmp/x4-cover-overlay-focused.log`,
`/tmp/x4-cover-overlay-all-tests.log`,
`/tmp/x4-cover-overlay-empty-before.log`,
`/tmp/x4-cover-overlay-focused-final.log`,
`/tmp/x4-cover-overlay-pio.log`, and `/tmp/x4-cover-overlay-pio-final.log`.
The pre-existing SDK edits remain unchanged and excluded from this checkpoint.
