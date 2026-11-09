# Combined first CrossInk port

This records the preserved integration candidate for the first two feature groups. It is not
full CrossInk feature parity and has not been flashed or physically tested.

- Base: CrossPoint 2.0.0, `42d58daaabcf469a2e5e1ba871714fb9dcf51d29`.
- CrossInk reference: v1.6.1, `9914146eeae7b46b300f475a16c32426fc02ec1f`.
- FreeInk SDK: unchanged at `5e94fe7480f019c89fbf691aef87523eb1b46704`.
- Personal version: `2.0.0-chad.2`.

The combined source keeps opt-in seven-slot top/bottom bars and reader-only
side/edge shortcuts, plus reading-time/pace collection and an initial saved
statistics screen. The detailed behavior and remaining gaps are in
[FEATURE_PARITY.md](FEATURE_PARITY.md), [VALIDATION.md](VALIDATION.md), and
[READING_STATS.md](READING_STATS.md).

The integration applies two independently checked source snapshots. Eleven
files overlap; five required explicit resolution: attribution, English
translation additions, the version string, Settings screen includes, and the
host test list. Both sets of additions were retained. Every single-source file
was compared byte-for-byte with its originating worktree and matched.

The statistics owner uses the existing render/transition lock and is separate
from the existing plugin-session accumulator. Remote turns now report their
outcomes through that existing accumulator as well. Statistics add a bounded
reader-lifetime object (compile-time limit 640 bytes, plus a cold directory
string); custom bars add fixed serialized-render scratch. No task, framebuffer,
SDK pin, cache-key algorithm, book path, or plugin API is replaced.

Statistics are preserved outside rebuildable caches, and old CrossInk formats
are read without rewriting their source during ordinary opening. The existing
finished-book move prepares statistics first. Book/global updates are separate
recoverable file transactions; a reset between them can leave a lasting totals
mismatch. No reconciliation journal is included in this slice.

## Integration verification

The corrected combined host suite passed **943/943** (10.87 seconds). Post-fix
cppcheck passed with no defects (31.896 seconds). Four independent integration
reviews and their one targeted follow-up are complete with no outstanding
findings. End-screen shortcuts now use the existing navigation dispatcher;
opaque statistics cannot block invalidation of rebuildable metadata/pages.
Final firmware builds passed:

| Target | Static RAM | Program flash | Outcome |
| --- | ---: | ---: | --- |
| X4 Pro | 112,268 bytes | 5,458,246 bytes | Passed |
| Default C3 | 67,208 bytes | 5,613,511 bytes | Passed |

The X4 Pro delta versus this checkout's clean 2.0.0 build is +504 bytes static
RAM and +43,220 bytes program flash. Its application slot is 6,553,600 bytes.
These figures do not measure runtime free heap or stack use. Existing
individual-group logs are retained in their separate worktrees. Do not substitute those for combined-build evidence.
Human acceptance of the combined behavior and maintenance tradeoffs is pending.

After acceptance, physical testing must cover custom bars in all orientations,
selection/lookup alignment, side-button holds across menu transitions, both
inline and full-screen menus, timed statistics including sleep and idle,
cache-preservation, and unchanged reading positions. Runtime internal/PSRAM
heaps, largest blocks, stack use and storage latency require measurement.

## Physical acceptance procedure

Use a fresh verified progress/settings backup before any device update. The
candidate has not been uploaded to the reader. Test cache deletion and file
replacement only with a disposable test book or a cloned card; the existing
clear-all-cache behavior also removes saved positions.

1. Confirm the displayed version begins with `2.0.0-chad.2` after an accepted update.
   Open an in-progress book and verify its current saved position.
2. With custom bars off, check the existing footer. Enable independent top and
   bottom layouts, rotate through all orientations, and verify text bounds,
   selection, dictionary/footnotes, page turns, and saved position.
3. Bind side taps/holds/chord and edge actions. A hold that opens a menu must
   not operate that menu on release. Stagger the two side holds. At an end
   screen with no suggested next book, Previous must return to the last page
   and Next must go Home, including resolved Default taps.
4. Read a test book for a timed minute with qualified forward turns. Spend a
   separate timed interval in the inline toolbar and in a full-screen child
   menu. Exit and inspect saved totals: menu intervals must not be included.
   A page interval longer than the chosen idle limit contributes no time.
5. Repeat sleep/reopen and end-of-book transitions. No duplicate session should
   be added by sleep preparation followed by reader teardown. Verify statistics
   and existing reading positions survive reopening.
6. On a disposable book, verify cache clear and same-path content replacement
   rebuild metadata/pages while retaining original statistics. Repeat with an
   unsupported statistics payload; the new book must not show old cached text.
7. Measure internal and PSRAM free/largest blocks, stack high-water marks and
   save/sleep latency during repeated reader/menu/dictionary transitions. The
   C3 target needs its documented free-heap margin. Build-time RAM accounting
   does not establish those runtime results.


## Gesture successor

`2.0.0-chad.3` builds on this source with continuous edge light controls and
multi-touch gestures. See [GESTURES.md](GESTURES.md) for that checkpoint's checks
and limitations. This file retains the `chad.2` measurements and review history.
