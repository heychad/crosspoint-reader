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
remain available. Reading statistics opens the existing Device/All Devices
screen when tracking is enabled. Current-book statistics from Home and the
richer Minimal/Dashboard presentation remain separate parity work.

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

Opening statistics uses checked allocation of the existing activity. Applying
the replacement releases Home and its cover cache before statistics loads data.
Allocation failure logs an error and leaves Home available. The standard X4 Pro
ELF reports 892 bytes for HomeActivity, 684 for its included OptionPopup, 1,940
for ReadingStatsActivity, and 24 per std::string. Seven option slots therefore
use 168 bytes plus label storage. No Home data member was added. These are
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
checks. The shared cover rectangles now drive both drawing and touch. All 2,284 host checks passed in 41.31 seconds. The expanded input checks also
passed with Carousel and deferred completion enabled under AddressSanitizer and
UndefinedBehaviorSanitizer. The standard X4 Pro build passed in 108.55 seconds;
the Carousel profile passed in 99.98 seconds. Device acceptance and cumulative
release review remain open.

Next device check: in each theme, try the four swipe directions, open one menu
entry, tap the previewed cover, and confirm reading position. Repeat cover taps
in all four orientations. Keep richer artwork, progress, Dashboard statistics,
and current-book statistics open in the parity tracker.

Source: CrossInk [v2.0.0-rc2 Home](https://github.com/uxjulia/CrossInk/blob/v2.0.0-rc2/src/activities/home/HomeActivity.cpp).
