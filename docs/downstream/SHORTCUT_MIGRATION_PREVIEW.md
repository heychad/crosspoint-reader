# CrossInk shortcut preview

`scripts/preview_crossink_shortcuts.py` compares selected CrossInk 1.6.1 X4 Pro
shortcut values with the personal fork's JSON settings. It reads files and prints
a JSON report; it does not modify a backup, device, or settings file.

```sh
python3 scripts/preview_crossink_shortcuts.py --legacy /path/to/crossink-settings.json --current /path/to/fresh-current-settings.json
```

Use a fresh current backup before acting on the report. Without one, the report
shows candidates but produces no patch. Existing current keys are always
preserved; a different value is reported as `existing_value_preserved`, even
when that value is invalid. `patch_for_missing_fields_only` is a proposal, not
an installed setting. Review and test each shortcut before using it on the X4
Pro. This helper does not apply or upload the proposal.

The source version is an assumption supplied by the caller; the helper cannot
verify the origin of the JSON. It translates only the listed Home and side
button actions, and the side-button chord. CrossInk uses different numeric
catalogs for single buttons and chords. Unknown or unsupported values are
reported, never silently substituted. Side bindings require CrossInk's
orientation-aware setting to be explicitly off; Home bindings require its
reader Home control to be explicitly enabled. Quick Actions bindings always
require manual review because the fork permits only one trigger. Other settings, the separate
on-device settings file, per-book overrides, and full migration remain open.
The preview remains conservative for orientation-aware side bindings even
though the experimental firmware can import them with the matching orientation setting.
CrossInk's font-switch action maps to the fork's font cycle, including Home
button gestures. CrossInk cycled
its built-in fonts; the fork also cycles through usable installed fonts. This
action is supported in EPUB reading; other reader formats can differ.
Home / Reader returns from stacked panels to the open book, or goes Home from
the reader. If a panel's cancellation starts another transition, that transition
takes priority. It does not interrupt an exclusive storage session.

The development firmware also offers **Import CrossInk controls** under Settings
> Controls. After confirmation, it reads `/.crosspoint/crossink-settings.json`,
backs up the current settings, and imports supported button bindings and Quick
Actions, including supported power, side, and Home gestures. An existing Quick
Actions trigger is preserved. Compatible page-turn, pinch, rotation,
two-finger swipe, and edge-slide settings are also imported. A conflicting two-finger swipe group
is left unchanged. The reader touch setting uses the effective combination of
CrossInk's touch controls and touchscreen-disable settings; it stays enabled
when disabling it would strand an existing edge Quick Actions trigger. Reader
menu touch behavior can still differ. Home gestures import only when enabled in both
the old and current reader settings; an omitted current value uses the enabled default. Unsupported values stay
unchanged; this does not import every CrossInk preference. The original source
is retained. **Check CrossInk import recovery** can resolve an interrupted
import only when the current settings exactly match the backup and no temporary
settings files remain. Other cases require inspection of the `.crosspoint`
folder. These paths need independent review and X4 Pro validation before use.

**Import CrossInk library settings** under Settings > Library separately imports
compatible series, genre, format and finished filters, recent-book layout,
filename extensions, file-browser row style, and supported home themes. Carousel
imports only when the experimental Carousel build is enabled. It uses the same backup and recovery process and
does not change other preferences. Library-first order imports only when the
source used CrossInk's Minimal or Dashboard home, where that preference applied.

**Import CrossInk preferences** under Settings > Reader imports UI scale,
statistics tracking, the reading idle threshold, refresh frequency, night mode,
battery-percentage visibility, sleep timeout, supported sleep settings, frontlight brightness, warmth, and on/off state, and compatible reading layout
settings. Line spacing imports only when it matches a supported preset; margins
import when both axes match and fit the supported range. Only the default word
spacing maps directly. RC character spacing imports when it represents an
exact whole-pixel value. Image display and its separate grayscale toggle map
together to the native grayscale or black-and-white mode; invalid combinations
are skipped. Dark, Light, Custom, Cover, Blank, Cover Custom, and Quick
Resume sleep screens map to their counterparts. CrossInk Minimal maps to the
fork's Minimal book screen, and Dashboard maps to Book dashboard. Reading Stats
maps to the fork's reading-statistics screen. On X4 Pro, Minimal Stats uses the
same underlying layout as Minimal. CrossInk Overlay maps to Page Overlay only when
the experimental Page Overlay build is enabled; other builds skip it. It also imports automatic
statistics backup when that experimental feature is enabled. Invalid values are
skipped. The import uses the same settings backup and recovery process.

For the first device check, back up the card and use a disposable settings
source. Cancel once and confirm the controls remain unchanged. Then import,
verify the supported actions and Quick Actions in all orientations, restart,
and confirm the choices and reading position remain. Check that the original
CrossInk file and the numbered settings backup are still present. Failure
injection has only been simulated; do not damage files to test recovery.
Behavior outside an EPUB reader, hold timing, orientation, and Quick Actions
conflicts can differ and need device checks.

The parser rejects duplicate keys, malformed JSON, and files over 1 MiB. The
report contains selected shortcut values only; unrelated settings and
credentials are never included. It does not perform any network operation.

The controls import also carries the Home-button enabled state. It never
disables Home while that button owns the current Quick Actions shortcut.
Existing native values remain unless the import explicitly replaces them.

The firmware importer accepts tagged CrossInk 2.0 RC power actions 37–40:
Home, Select Chapter, Rotate 180°, and Toggle Page Gestures. Both short and long
power bindings use the same catalog. Side-only values 64–70 are rejected for
power bindings. Wake Only remains unsupported, and Quick Actions still uses
the separate ownership check. These import changes need the next device check;
the read-only Python preview above has its own narrower scope.

RC edge values 10–12 also import: Home/Reader, Select Chapter, and Toggle Page
Gestures. The four edge pickers now scroll through all supported actions.
Their display indices differ from stored Action IDs for the three additions;
the Web Settings API uses display indices, while settings files retain raw IDs.
Existing indices 0–33 are unchanged. Verify these bindings after restarting the
reader during the next device check.
