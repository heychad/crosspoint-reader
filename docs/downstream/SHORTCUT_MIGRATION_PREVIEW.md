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
Behavior outside an EPUB reader, hold timing, orientation, and Quick Actions
conflicts can differ and need device checks.

The parser rejects duplicate keys, malformed JSON, and files over 1 MiB. The
report contains selected shortcut values only; unrelated settings and
credentials are never included. It does not perform any network operation.
