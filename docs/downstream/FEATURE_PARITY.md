# CrossInk feature parity

Target: bring the selected CrossInk features, including reviewed RC updates, into
this CrossPoint-based fork for
the Xteink X4 Pro. Preserve CrossPoint's EPUB engine, plugin/protected-book
interfaces, and current book-cache identities. Neither a source import nor a
successful build alone establishes device-level feature parity.

Source revisions:

- CrossPoint stable base: `42d58daaabcf469a2e5e1ba871714fb9dcf51d29` (2.0.0).
- This candidate excludes CrossPoint development merge `6bea3810`; the original branch preserves it.
- Original CrossInk reference: `9914146eeae7b46b300f475a16c32426fc02ec1f` (v1.6.1).
- CrossInk RC reference: `b8feaec4d` (2.0.0-rc). The earlier development comparison at `1990a78f2` is historical, not an integration target.
- FreeInk SDK: `5e94fe7480f019c89fbf691aef87523eb1b46704` (CrossPoint 2.0.0 stable pin).

## Selected first-release scope: current source snapshot

The table describes the current local candidate. “Built” means implemented with
local checks, not accepted on the X4 Pro. Chad installed the test58-based
clipfix2 and confirmed clipping, saved-content navigation, cancellation across
orientations, page turns, Quick Lock, sleep/resume, and direct Home covers.
Subsequent feature work is not installed or physically tested. The historical
inventory below records an earlier stage and is not the current status.

| Choice | Current state |
| --- | --- |
| A1 Custom top and bottom bars | Built, including full-width filled progress bars and book/section time-left. Test2 bars were checked on the reader; later changes need device checks. |
| A2 Button remapping and shortcuts | Built for the selected X4 Pro inputs: side tap/hold and orientation handling, Power tap/hold, Power+Up, Up+Down, and Home tap/hold/double-tap. The [RC catalog audit](RC_SHORTCUT_AUDIT.md) covers mappings and ownership. Actions retain their hardware, format, and experimental-feature gates. Device acceptance remains. |
| A3 Touch gestures and Quick Lock | Built; hardware and interaction checks remain. |
| A4 Five-slot Quick Actions | Built; the reader menu and toolbar More panel now provide direct access as well as shortcut triggers. Hardware checks remain. |
| A5 Per-book reading layout | Built for native reader layouts, including independent 5–150-pixel horizontal/vertical margins, settings and tabbed-menu sliders, preview, persistence, reset, and compatible binary imports. Old common-margin saves initialize both axes. Unsupported fields and Safe Mode migration remain under G3; device acceptance remains. |
| A9 Extra actions and legacy mappings | Built for the selected X4 Pro scope. The tagged RC catalog audit covers power, side, chord, Home, Quick Actions slots, and gesture mappings. Includes Home/Reader, chapter selection, rotation, network entry, and distinct power sleep/wake modes. Invoked experimental features keep their own gates; cumulative review and device checks remain. |
| B1 Format and finished-book filters | Built; completion data that cannot be read stays visible. |
| B2 Compact rows and filenames | Built. |
| B3 Browse by genre and series | Built from EPUB metadata; hardware checks remain. |
| B4 Recent-books cover grid | Built; hardware checks remain. An optional Title shelf grid is also available independently. |
| B5 Mark finished and organize completed books | Finished status can be read for filtering. Experimental reader menus can mark finished or unfinished and apply Recents removal. A separately flagged prototype records a deferred move for an open EPUB and attempts it when Home opens. It disables the old unjournaled end-of-book move and needs lifecycle, recovery, storage ownership, and device checks before use. Journal rollover is prototyped. |
| B6 Unified Saved Items | Built with reader-menu, button, and Quick Actions entry. Saved clipping text and navigation passed on clipfix2. First-open latency and the page flash before the list remain polish follow-ups. Older CrossInk bookmarks are read-only in Saved Items; the reader can clear all verified bookmarks for its open book. |
| B7 Full CrossInk menu and navigation parity | Built; the five-tab EPUB menu has 12 inline text/layout controls, font panes, per-book tracking, gated statistics and reading-pace reset, confirmed cache reset that preserves reader state, confirmed bookmark clearing, per-book dictionary selection, and chapter-indexing controls. List and toolbar remain available. Both reader formats share statistics-reset handling. The [RC menu audit](EPUB_MENU_AUDIT.md) records the controls and bounded live text preview. Cumulative review and device validation remain. |
| C1 Reading statistics and time-left | Built for supported tracking data; book, device, and All Devices views show page-turn pace, and reader bars estimate book and current-section time. An experimental table-of-contents chapter estimate is available behind a build flag; ambiguous boundaries show no estimate. It needs review and device checks. |
| C2 Edit dates and per-book tracking | Dates and tracking are available for an actively tracked book. Library statistics can edit dates and tracking for a closed book with readable existing statistics; device checks remain. |
| C3 All Devices statistics | Built for compatible statistics files already present locally; it does not transfer them or detect absent device files. The statistics screen and charts report simultaneous save and partial-data warnings together. |
| C4 History charts and trends | Built: time-of-day, weekday, reading-day calendar, and 7/30/365-day counts. The optional daily recorder adds Device rows for recorded Today and a 7-day daily average, preserving unknown data and partial warnings. Default firmware leaves daily recording disabled; cumulative storage review and device validation remain. See [daily statistics](DAILY_READING_STATS.md). |
| C5 Statistics backup, restore, reset | Manual device and individual-book backups are built. Experimental device restore/reset, closed-book restore, and automatic device backups, including separately named undated snapshots, are available behind build flags; review and device checks remain. Dated, manual, and numbered undated groups each retain seven after checked scans; legacy hash-only undated files are preserved. Release review and device validation remain. CrossInk's backup covers device totals, not a whole-library backup. |
| C6 Near-end completion prompt | Experimental prompt and live completion save are wired behind separate build flags. It uses optimizer character or word metadata when valid, falling back to byte-weighted progress, and supports tracking being off without enabling it. A further experimental flag allows it to request a deferred move; lifecycle, recovery, device testing, and final review remain. |
| D1 Google Drive to reader sync | Experimental batch transfer and recovery prototypes exist; security, storage ownership, responsiveness, review, and hardware validation remain. |
| E1 Extra home-screen themes | Cover-grid home and initial Minimal and Dashboard layouts built. An experimental Carousel adds cover selection, touch and swipe controls, a horizontal home menu, saved reading progress, and reading time. Its responsive geometry and matching hit areas are implemented; visual treatment and device input parity remain incomplete. See [Carousel](CAROUSEL_HOME.md). Library import maps Carousel only in the experimental build. Device checks remain. |
| E2 Extra sleep-screen dashboards | Reading-statistics, Reading summary, cover summary, Minimal book, and Book dashboard sleep screens built. Without a clock, Reading statistics also shows locally available All devices totals, with an explicit partial-data warning. The latter two show progress without requiring statistics and use a title fallback when no cached cover is available. Book dashboard can show the saved EPUB section's chapter title, start date, daily average, and current device streak when their data is available. Experimental Page Overlay uses retained screen content and CrossInk image locations; its favorite and folder controls use a recoverable state writer. Normal state saves now use that same checked writer. Reader and Home show state-save warnings; Home Retry saves current RAM state after recovery and can retry failures with no artifacts. Pending completion journals and unresolved artifacts block recovery. The loaded reader now reconstructs EPUB/TXT/Markdown/XTC/XTCH pages before teardown, including materialized pages from unfinished builds. Stale layouts fall back without changing progress. When no overlay art is available, the loaded reader now uses its normal grayscale display path before teardown. Unavailable-layout reconstruction, visual acceptance, and device checks remain. |
| E4 Small/large UI scale | Built; display fit needs hardware checks. |
| G1 Rendering controls and recovery | Manual Default, Balanced, and Light profiles built. Known chapter-build memory failures retry progressively with simpler profiles, then without embedded styling or focus mode, for the current session. Disabling styles can reveal text hidden by the publisher's CSS. The last known text offset is preserved when available. Other failures do not trigger this retry. The fallback, message, and device behavior need review and testing. |
| G2 Silent next-chapter prefetch | Experimental scheduler builds the next EPUB section near the end of the current one, with input cancellation and heap/radio guards. It requires the experimental build flag; review, responsiveness measurements, and device validation remain. |
| G3 Complete old-settings migration | Partial; embedded edge gestures, supported side-button bindings and orientation, long-power and Power+Up bindings, and compatible Quick Actions slots import when modern keys are absent. CrossInk font-switch bindings import as the fork's font cycle, which also includes usable installed fonts. Separate imports cover supported controls, touch gestures, library filters and layouts, compatible reading and sleep settings, and top and bottom bars; conflicting two-finger swipe groups stay unchanged. CrossInk chapter-time slots import when experimental chapter time is enabled; stable-page slots and chapter time in other builds are refused without changing current bars. Compatible per-book values from CrossInk formats 1–13, including an independently flagged rendering profile, can import when native book preferences are absent. The explicit preferences import also maps exact whole-pixel RC character spacing, the combined image/grayscale choice, and frontlight state. Controls import the Home enable state without disabling a current Quick Actions owner. Unsupported Quick Actions slots and triggers, other global settings, and remaining per-book fields need migration. |
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

## RC shortcut update

November 11 follow-up: the power importer now accepts RC actions 37–40
(Home, Select Chapter, Rotate 180°, and Toggle Page Gestures). Its older cutoff
had rejected these values despite their presence in the shared mapper. Unknown
power values and side-only extensions remain rejected; existing native hold
bindings retain precedence. All 34 focused shortcut/import checks pass.

The RC edge follow-up adds Home/Reader, Select Chapter, and Toggle Page Gestures
to all four edge bindings and accepts legacy values 10–12. The picker translates
display indices while settings files retain existing Action IDs. It uses a
scrolling list for all 37 options. Invalid stored IDs reset to Default; existing
Quick Actions ownership is preserved. Device checks remain pending.

Sleep Only and Wake Only now import as distinct power actions. The short-press
wake policy follows the active Power tap binding, while Default preserves the
legacy setting. Wake Only consumes an awake tap; Sleep Only requests sleep.
These modes stay out of side, edge, and Quick Actions pickers. The
[RC shortcut catalog audit](RC_SHORTCUT_AUDIT.md) now covers the selected X4 Pro
scope, including every byte value for the main mapping catalogs and all source
Quick Actions owners. Cumulative review, unfinished experimental actions, and
physical checks remain open.

Select Chapter is available in the reader action catalog, Quick Actions, Home
button mappings, and two-finger swipes. EPUB uses the existing chapter picker;
cancelling a direct shortcut returns to the book, while cancelling a menu entry
returns to the reader menu. XTC offers the action only for books with chapters.
Existing persisted action IDs remain unchanged; new two-finger Home,
Home/Reader, and Select Chapter values match CrossInk's 9, 10, and 11.
CrossInk raw Select Chapter value 38 imports for Home and ordinary side/power
bindings. Legacy edge imports reject actions outside that edge's palette.

Validation: all 1,781 host tests passed; the X4 Pro build passed in 144.32
seconds. The original selected-feature rows remain partial where other actions
or mappings are missing. Device validation and release review remain pending.

## RC mapping follow-up

Home-button bindings now also expose the existing Home/Reader, Rotate 180°,
Cycle Auto Turn, Focus Reading, and Toggle Home Button actions. Native IDs are
appended so older settings keep their meaning. CrossInk imports recognize the
corresponding Home values and the additional side/power and chord values for
Home, Rotate 180°, Select Chapter, and Cycle Auto Turn.

The subsequent batch adds the independent page-gesture toggle and direct
network-mode entry points described below. Guide dots remain in the Later
scope; Nearby stays skipped. This earlier checkpoint left A2, A9, and G3 open; the later [RC catalog audit](RC_SHORTCUT_AUDIT.md) closes selected X4 Pro mapping coverage. G3 remains open.

The mapping follow-up passed 41 focused input, navigation, and migration checks
and the X4 Pro firmware build (120.34 seconds). It has not been tested on the
reader or through the release-review process.

## Page gestures and direct network entry

The independent Disable page-turn gestures setting blocks page-turn taps and
swipes without changing their configured modes or disabling reader menus,
links, selection, or custom gestures. It persists under CrossInk's
`disablePageTurnGestures` key. Its shortcut restores the previous value if
saving fails and consumes the current touch contact.

Join Network, Create Hotspot, and Calibre Wireless shortcuts select the matching
existing File Transfer mode. They retain the normal Wi-Fi selection, startup,
shutdown, and reading-state lifecycle. The general File Transfer shortcut still
opens the mode picker. Quick Actions and Home/side/power mappings expose the new
actions; CrossInk's corresponding raw values import. The page-gesture toggle is
also available as a two-finger action with CrossInk value 12.

No device has been updated with this batch; physical validation remains open.

This batch passed all 1,784 host tests and the X4 Pro firmware build
(143.98 seconds). Release review and hardware validation remain pending.

## Historical Quick Actions trigger audit

This development-only addition is excluded from the release-baseline candidate;
Home double-tap remains supported. The following records its earlier checks.

The power double-click action introduced by the upstream update now participates
in the existing single-trigger rule. Assigning Quick Actions there clears the
previous trigger; assigning it elsewhere restores power double-click to its
normal frontlight action. Ordinary double-click actions do not claim ownership.

The trigger fix passed 24 targeted checks and the X4 Pro build (77.76 seconds).
Hardware and release review remain pending.

## RC per-book decoder

CrossInk per-book settings versions 11–13 are now decoded alongside versions
1–10. Compatible owned fields import when native preferences are absent.
The preview preserves unsupported fields, and the original source remains
unchanged. Exact whole-pixel character spacing is supported. Image grayscale
maps only with a matching explicit image-display override. Font migration,
Safe Mode, half-pixel tracking, and other unsupported per-book fields remain
open; this does not complete G3.

The RC decoder passed 1,808 reference and malformed-record cases, 43 relevant
host checks, and the X4 Pro build (69.94 seconds). Device validation remains
pending.
