# CrossInk RC shortcut audit

November 11, 2026. Reference: CrossInk `v2.0.0-rc`, commit
`b8feaec4d10ecb484ed80e326f01f2a3ea949331`. The fork remains based on stable
CrossPoint 2.0.0. This audit covers mapping and trigger support for the X4 Pro;
it does not certify hardware behavior or complete feature parity.

## Catalog coverage

| Catalog | Supported mappings | Handling |
| --- | --- | --- |
| Power tap/hold | 35 direct mappings | Includes separate Sleep Only/Wake Only and RC actions 36–40. Quick Actions uses its ownership check. |
| Side tap/hold | 40 direct mappings | Shared actions plus seven side-only chapter, size, and rotation values 64–70. |
| Power+Up and Up+Down | 32 direct mappings each | Uses the separate chord catalog. Removed values 2–3 are rejected. Quick Actions uses its ownership check. |
| Home tap/hold/double-tap | 37 direct mappings | Preserves Home-specific values 23–25. Quick Actions uses its ownership check. |
| Five Quick Actions slots | 32 accepted source values, including empty | Preserves all five slots together. Unsupported values reject the group; existing slots remain. |
| Edge swipes | All 13 RC values | Converts to native Action IDs; the scrolling picker and reload validation preserve the three RC additions. |
| Two-finger swipes | All 13 RC values | Keeps the RC values. Duplicate actions retain the existing single-direction policy. |

The native import tests check every value from 0 through 255 for power, side,
chord, Home, and Quick Actions slots. Unsupported values leave the destination
unchanged. These tests use the tagged RC's persisted catalogs, not menu order.
They do not prove that every mapped action is available in every context.
Existing hardware, reader-format, and experimental-feature gates still apply.

## Scope decisions

- Guide dots remain Later. A slot group containing that unsupported action is
  preserved rather than partially imported.
- Nearby sync remains Skip.
- Long Back/Menu and front Left/Right hold triggers are not X4 Pro inputs. Its
  pinned SDK profile has physical Up, Down, and Power buttons, plus touchscreen
  Home. Do not add phantom controls to imitate another board.
- Quick Lock stays out of edge and Quick Actions slot pickers because it needs
  an identifiable physical unlock gesture. Quick Actions cannot open itself.
- Power-only modes stay out of non-power pickers. Home's old Sleep Only value
  remains a compatibility alias for Sleep; Home does not control wake policy.

## Quick Actions ownership

Eleven RC physical triggers apply to the X4 Pro: Power tap/hold/Up chord, Home
tap/hold/double-tap, Up+Down, and four side tap/hold bindings. All have migration
paths. Current ownership and conflicting source bindings are preserved.

The source-owner check now inspects all 15 RC fields, including the four
non-X4-Pro inputs. A source containing a conflicting front-button owner no
longer imports a second owner. A source selecting a front-only trigger does
not install a phantom X4 Pro binding. The added fields extend the static
inspection table; they add no input route, task, or allocation.

## Evidence and remaining work

`RcShortcutCatalogTest.cpp` covers the catalog ranges and front-owner conflict.
The 40 focused catalog/import tests pass. Earlier gesture tests cover RC edge
values, sparse picker persistence, cancellation, and allocation failure.
The default X4 Pro build passed in 65.56 seconds. The prior full host suite
passed all 1,781 tests before this bounded ownership-table change.

The catalog audit is complete for the selected X4 Pro scope. Cumulative release
review, enabling unfinished experimental features, and device validation remain.
The audit must be revisited when selecting a different CrossInk tag.
