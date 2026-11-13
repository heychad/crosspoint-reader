# Compact Home controls

Minimal and Dashboard Home now use the tagged RC2 gesture flow on the X4 Pro:

| Input | Action |
| --- | --- |
| Swipe up | Open Menu |
| Swipe right | Open the preferred browser: Files or Library |
| Swipe down | Open Settings |
| Swipe left | Preview the next available recent book |
| Tap the cover or Confirm | Open the previewed book |
| Back | Open Menu |

The regular vertical menu and button hints are hidden on these two Home screens.
Other Home themes retain their existing controls. The X4 Pro has two navigation
buttons, Power, touch, and a capacitive Home key; this does not import raw
four-front-button bindings from other CrossInk boards.

Menu reuses the existing option popup and its input-publication handshake,
remapped navigation, Confirm/Back handling, and outside-tap dismissal. Both
browsers, Saved Items, transfer, settings, and the existing plugins/OPDS entry
remain available. Reading statistics opens the previewed book's Book tab, with Device and
All Devices still available. With no previewed book it opens the device view.
The normal closed-book date, tracking, completion, backup, and recovery controls
retain their existing build flags and availability guards. The richer presentation is now built; see [Home presentation](HOME_PRESENTATION.md).

## Reading state and resources

Cycling recent books reads the existing recent list, skips unavailable files,
and replaces the one Home preview. It does not reorder or save Recents, change
the open-book path, or write reading progress. The normal reader updates those
values only after the book is opened. Thumbnail generation retains its existing
derived-cache behavior.

The change holds RenderLock while replacing the preview, invalidating the
existing cover snapshot, and refreshing Dashboard data through its read-only
loader. The old snapshot is released before the next render. It does not retain
a second book list or add a popup instance, setting, persistent file, worker, or
network connection. The reused popup holds at most seven option strings; its
local pointer array is 28 bytes on the X4 Pro. Normal string capacity and the
existing popup interaction storage remain in use.

Home and Library now share `ReadingStatsActivity::forClosedBook`. It checks the
book's presence, uses read-only book/global loading, and retains the existing
closed-book edit and recovery flow. Missing or unknown tracking data disables
book writes. EPUB and XTC-family progress use the saved cache; TXT/Markdown
progress remains unavailable because its validator can delete stale layouts.
No repair or migration is introduced by this loader.

Opening statistics uses checked allocation of the existing activity and a
bounded temporary snapshot. Home releases its cover cache, then stays on the
activity stack so Back returns to the preview. If that book was removed from
Recents, moved, or deleted through statistics, Home reloads the available list.
Dashboard data refreshes read-only. Failed loading or allocation leaves Home
available and opens the existing book-statistics error message on the next
input pass, without reentering the popup's selection callback. The earlier compact-controls X4 Pro
ELF reported 892 bytes for HomeActivity, 684 for its included OptionPopup, 1,940
for ReadingStatsActivity, and 24 per std::string. Seven option slots therefore
use 168 bytes plus label storage. The book-statistics stage adds a failure-notice flag without increasing the 892-byte Home object. Its temporary book/global snapshot is 244 bytes (80 + 164), bounded to 512 bytes by a compile-time check. These are
object sizes, not peak-heap measurements; the Home cover cache and existing
renderer/UI owners also coexist during allocation. Runtime heap, latency, and
physical gesture behavior remain unmeasured.

## Verification and remaining work

The complete production Home input method is checked for both compact themes,
both browser orders, plugin/OPDS combinations, and tracking enabled/disabled.
Checks cover all menu destinations, modal ownership, swipes with a simultaneous
synthetic Back/Confirm, next-book wrapping, missing files, unchanged Recents,
cover-cache invalidation under RenderLock, refreshed Dashboard data, exact cover
hit bounds in portrait/landscape, empty books, and statistics allocation failure.
The normal Home and Carousel configurations remain covered.

Minimal and Dashboard rendering still use the existing cover-cache and fallback
checks. The shared cover rectangles now drive both drawing and touch. The compact-controls checkpoint passed all 2,284 host checks in 41.31 seconds. The expanded input checks also
passed with Carousel and deferred completion enabled under AddressSanitizer and
UndefinedBehaviorSanitizer. The standard X4 Pro build passed in 108.55 seconds;
the Carousel profile passed in 99.98 seconds. Device acceptance and cumulative
release review remain open.

The book-statistics stage's full run passed 2,277/2,284 checks. Seven UI fixtures
lacked the new loader's XTC dependency. Moving the unchanged loader into
`ReadingStatsActivityClosedBook.cpp` restored the original drawing source and
kept file-loading dependencies separate. The eight affected UI/Home checks then
passed in 1.84 seconds; the shared factory/Library check passed directly. Final
Home checks also cover returning after removal of the last recent book under
ASan/UBSan. Final standard X4 Pro build: 71.41 seconds, 113,132 bytes static RAM,
5,660,466 bytes application flash. The combined X4 Pro profile passed in 79.96
seconds: 113,060 bytes RAM and 5,741,942 bytes flash. It enables Carousel,
completion/moves/prompt, statistics restore/daily recording, and Page Overlay;
it does not claim coverage of every experimental flag. In that ELF, Home is
944 bytes and ReadingStatsActivity is 2,452 bytes; the snapshot remains 244
bytes. Physical behavior and peak heap still need device checks.

Next device check: in each theme, try the four swipe directions, open one menu
entry, tap the previewed cover, and confirm reading position. Repeat cover taps
in all four orientations. Also open statistics for a previewed book, return to Home, and confirm that the
preview and saved position are retained. Check the richer artwork, progress, and Dashboard fields described in [Home presentation](HOME_PRESENTATION.md).

Source: CrossInk [v2.0.0-rc2 Home](https://github.com/uxjulia/CrossInk/blob/v2.0.0-rc2/src/activities/home/HomeActivity.cpp).
