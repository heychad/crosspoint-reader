# CrossInk feature parity

Target: bring CrossInk v1.6.1 features into this CrossPoint 2.0.0-based fork for
the Xteink X4 Pro. Preserve CrossPoint's EPUB engine, plugin/protected-book
interfaces, and current book-cache identities. Neither a source import nor a
successful build alone establishes device-level feature parity.

Source revisions:

- CrossPoint: `42d58daaabcf469a2e5e1ba871714fb9dcf51d29` (2.0.0).
- CrossInk: `9914146eeae7b46b300f475a16c32426fc02ec1f` (v1.6.1).
- FreeInk SDK: `5e94fe7480f019c89fbf691aef87523eb1b46704` (CrossPoint pin).

## Selected first-release scope: current source snapshot

The table describes the current local candidate. “Built” means implemented with
local checks, not accepted on the X4 Pro. The reader previously ran test2; the
later bundle has not been installed or physically tested. The historical
inventory below records an earlier stage and is not the current status.

| Choice | Current state |
| --- | --- |
| A1 Custom top and bottom bars | Built, including full-width filled progress bars and book/section time-left. Test2 bars were checked on the reader; later changes need device checks. |
| A2 Button remapping and shortcuts | Reader bindings and additional Home-button actions built; full CrossInk mapping parity remains. |
| A3 Touch gestures and Quick Lock | Built; hardware and interaction checks remain. |
| A4 Five-slot Quick Actions | Built; hardware checks remain. |
| A5 Per-book reading layout | Native overrides built; compatible CrossInk binary values can load when native preferences are absent. Unsupported fields and Safe Mode need further migration work. |
| A9 Extra actions and legacy mappings | Many actions built, including Saved Items and File Transfer; the full legacy action mapping remains. |
| B1 Format and finished-book filters | Built; completion data that cannot be read stays visible. |
| B2 Compact rows and filenames | Built. |
| B3 Browse by genre and series | Built from EPUB metadata; hardware checks remain. |
| B4 Recent-books cover grid | Built; hardware checks remain. An optional Title shelf grid is also available independently. |
| B5 Mark finished and organize completed books | Finished status can be read for filtering. For EPUBs, existing end-of-book settings can remove Recents and defer a move to `/read`; explicit marking is not integrated. |
| B6 Unified Saved Items | Built with reader-menu, button, and Quick Actions entry. Older CrossInk bookmarks are read-only. |
| B7 Full CrossInk menu and navigation parity | Partial; EPUB reader entries include Home Button, Library, Browse Files, File Transfer, and Saved Items, and an optional Library-first Home order, but full menu parity remains. |
| C1 Reading statistics and time-left | Built for supported tracking data; reader bars estimate book and current-section time. A true table-of-contents chapter estimate remains. |
| C2 Edit dates and per-book tracking | Dates and tracking are available for an actively tracked book. Library can edit dates and change tracking for a closed book with writable saved statistics; device validation remains. |
| C3 All Devices statistics | Built for compatible statistics files already present locally; it does not transfer them or detect absent device files. If a snapshot save and aggregate load both fail, the save warning currently takes precedence over the partial-data warning. |
| C4 History charts and trends | Time-of-day, weekday, and device reading-day charts built; the device view shows recorded reading days for the last 7 and 30 days. Richer trends remain. |
| C5 Statistics backup, restore, reset | Manual device and individual-book backups built; restore, reset, and whole-library backup remain. |
| C6 Near-end completion prompt | Not in this candidate. |
| D1 Google Drive to reader sync | Incomplete; the experimental build has read-only inventory, while upload and publication remain unavailable. |
| E1 Extra home-screen themes | Cover-grid home and initial Minimal and Dashboard layouts built; full CrossInk theme parity and device checks remain. |
| E2 Extra sleep-screen dashboards | Reading-statistics, Reading summary, and cover summary sleep screens built; further CrossInk variants remain. |
| E4 Small/large UI scale | Built; display fit needs hardware checks. |
| G1 Rendering controls and recovery | Manual Default, Balanced, and Light profiles built; automatic fallback and recovery remain. |
| G2 Silent next-chapter prefetch | Experimental draft is disabled pending recovery and setup fixes; it covers only small cached text chapters. See [prefetch](PREFETCH.md). |
| G3 Complete old-settings migration | Partial; embedded edge-gesture keys import when modern global keys are absent. Compatible per-book values can import when native book preferences are absent. The separate CrossInk settings file and remaining per-book fields need migration. |
| G4 Personal automatic update channel | Fork release configuration prepared, but online checking and installation are disabled pending authentication. |
| G5 Optional developer diagnostics | Read-only X4 Pro memory diagnostics built; hardware checks remain. |

Nearby device-to-device sync is skipped. The features Chad selected for Later
remain deferred. Page-turn, rendering, and image performance tuning follows the
selected feature work; no speed improvement is claimed without measurement.

## Historical baseline inventory

The following inventory and the remaining sections describe the first integration
stage and are retained for history; their status statements are not current.

| Group | Required behavior | Status |
| --- | --- | --- |
| Status bars | Separate top/bottom slots, progress bars, precision | Implemented; host/build checks passed; device testing pending |
| Status-bar details | Time left, stable page numbers, per-format parity, tap to hide, live editor preview | Not yet ported |
| Button controls | Independent side tap/hold bindings and side-button chord | Implemented; host/build checks passed; device testing pending |
| Gesture controls | Left/right edge actions | Continuous brightness/warmth and release-triggered edge actions implemented; 989 host tests, static analysis and both builds passed; device checks pending |
| Gesture controls | Pinch font resizing, two-finger rotation and four-direction actions | Implemented in gesture stage; checks/reviews complete, human/device acceptance pending |
| Shortcut catalog | Power/Back/Menu/Home bindings, reader-only front remap, orientation-aware mappings, all CrossInk actions | CrossPoint has a subset; full catalog not yet ported |
| Quick Actions | Configurable menu and trigger ownership | Not yet ported |
| Quick Lock | Lock/unlock gestures and sleep behavior | Not yet ported |
| Typography | Lexend Deca, Bitter, Inter, supplemental/music glyphs, font sizing and TTF options | CrossPoint has fonts/TTF support; semantic comparison pending |
| Reading modes | Focus Reading, guide dots, forced paragraph indents | Focus Reading baseline; other behavior not yet compared/ported |
| Rendering modes | CrossInk default/balanced/light/safe rendering and indexing controls | Not yet compared/ported; preserve current upstream parser improvements |
| Pagination | Publisher page markers and optimizer-generated stable reference pages | Not yet ported |
| Reader menus | Five-tab menu, previews, per-book overrides and reset to global | CrossPoint toolbar exists; CrossInk behavior not yet ported |
| Reading progress | Current CrossPoint cache paths and safe progress writes | Protected baseline; no cache-key migration in this patch |
| Book completion | Manual/99% completion, read-folder moves, recents handling | CrossPoint has end-of-book support; stats/folder integration not yet ported |
| Reading statistics | Per-book/global sessions, time/pages/rates, history and editing | Collection, compatible persistence and initial totals screen integrated; charts/editing pending |
| Backup/import | Statistics backup, restore and compatibility with old saved data | Not yet ported |
| Nearby sync | Device-to-device statistics merge and reading-position sync | Not yet ported |
| Home/library | Recent grid, library filters/sorting and CrossInk layouts | CrossPoint cover grid exists; full comparison pending |
| Themes | Minimal and Dashboard themes | Not yet ported |
| Sleep screen | Dashboard/statistics, favorites and cover/custom combinations | CrossPoint sleep options exist; full comparison pending |
| Auto page turn | CrossInk interval range/custom value and control shortcuts | CrossPoint rates exist; parity pending |
| Frontlight | Time-based brightness/warmth, wake restore, continuous gesture control | CrossPoint basic frontlight exists; parity pending |
| Web tools | CrossInk settings/status-bar editor, EPUB optimizer, statistics/sync interfaces | Not yet ported; keep current CrossPoint plugins and protected-content routes |
| Personal maintenance | Fork releases and update channel | Updater still targets upstream CrossPoint; personal release channel is pending |
| Other changes | Remaining settings, utilities and behavior not covered above | See SETTING_GAPS.md; continue source/release audit |

CrossInk's documentation is an entry point, not the complete inventory:
[Reader features](https://github.com/uxjulia/CrossInk/blob/v1.6.1/docs/reader-features.md),
[controls](https://github.com/uxjulia/CrossInk/blob/v1.6.1/docs/controls.md), and
[release history](https://github.com/uxjulia/CrossInk/releases).

## First implementation

The first feature group adds an opt-in **Reader bar layouts** screen under
Settings > Reader. It preserves the legacy footer while disabled. Each bar has
three left slots, a center slot and three right slots. Supported items initially
include titles, clock/date, battery, chapter page count and book percentage.

Settings > Controls gains reader-only side-button tap/hold, side-button chord,
and left/right edge swipe actions. Default leaves the existing controls in
place. This does not yet replicate the complete CrossInk shortcut catalog or
every remaining power/front-button action. The gesture stage adds continuous light control and multi-touch; see [GESTURES.md](GESTURES.md).

Fixed renderer scratch is owned by the serialized render path. The input
tracker stores two small contact states, creates no task, and allocates no
heap. Reading-state paths, plugin routes, partitions and the pinned SDK remain
unchanged. Layout edits change the viewport; the current visible-text position
is retained before the existing section cache rebuild path is used.

## Validation gates

- Clean baseline X4 Pro build: passed; static RAM 111,764 bytes, flash 5,415,026
  bytes. The application slot is 6,553,600 bytes. This is build-time accounting,
  not runtime free heap.
- Final first-group build: passed on X4 Pro with 112,260 bytes static RAM and
  5,440,750 bytes flash (+496 RAM / +25,724 flash vs baseline). Default C3 also
  passed, using 67,208 bytes static RAM and 5,594,465 bytes flash. Runtime heap
  and hardware behavior are unverified.
- Host tests: settings/layout and shortcut state-machine cases in
  `test/reader_personalization`; 902/902 host tests passed serially for the final
  candidate. Parallel ParserStopTest cases share a temporary filename and also
  fail on the untouched baseline. Final X4 Pro cppcheck passed at all severities.
- Independent correctness, architecture, embedded-resource and i18n/docs reviews:
  initial and one targeted follow-up round complete. Two final follow-up
  findings received contained fixes and main-agent regression checks; see
  VALIDATION.md for coverage. Human design acceptance remains pending.
- Physical reading, all four orientations, input remaps, repeated transitions,
  heap behavior, sleep/resume and frontlight checks: pending.

The combined first two groups are preserved as `2.0.0-chad.2`; see
[INTEGRATION.md](INTEGRATION.md). At that stage no custom firmware had been
flashed, and the existing reader backup and CrossPoint firmware were the recovery baseline.

## Next integration pointers

The pinned FreeInk SDK already exposes multi-touch contacts, completed swipes,
rotation and pinch events in `libs/hardware/InputManager/include/InputManager.h`
and implements gesture classification in `src/MultiTouchGestureMath.h`.
The gesture stage exposes those contacts and events through `lib/hal/HalGPIO`
and the mapped-input layer. It retains the pinned SDK and its hardware drivers.

The current updater's release source remains in
`src/network/OtaUpdater.cpp`. Personal release publication and OTA configuration
must be completed before relying on it to maintain this downstream firmware.

The next checkpoint, `2.0.0-chad.3`, integrates gestures on top of that combined
source. See [GESTURES.md](GESTURES.md) for exact behavior, review coverage,
resource accounting and the physical verification pending at that stage.
