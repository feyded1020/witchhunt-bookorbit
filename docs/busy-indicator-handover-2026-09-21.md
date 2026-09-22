# Busy indicator — handover, 2026-09-21

Branch: `feat/busy-indicator` off `master` (594cf090a).

Written for whoever picks this up next, including a future session. It assumes familiarity with
the display stack but not with this work.

## Where we are

A busy indicator exists and runs on device, but **it is not ready to ship**. The measurements
below say the current mechanism costs more than it returns on X4 Pro, and the fix for that is
infrastructure work that has not been done yet.

Implemented:

- `CrossPointSettings::showBusyIndicator` (default on) + a Display toggle, so it can be A/B'd
  on device without a reflash.
- `BaseTheme::drawBusyIndicator()` — screen-centred, icon-only, an hourglass drawn as strokes.
  Mirrors `drawPopup()` exactly: same 2px frame idiom, same `overlayDisplayedFrame` sync rule,
  same `shipPopup()` ship, returns the same `Rect`.
- `ActivityManager::showBusyIndicator()` — decides *whether*, then calls the theme. It does not
  draw, sync or ship itself.
- `Activity::suppressesBusyIndicator()`, checked on **both** ends of a transition. `true` for
  `SleepActivity`, `FrontlightPanelActivity`, `BootActivity`.
- `ActivityManager::framebufferPreparedThisTick`, set by `dispatchLightPanelGesture()`.

## What the device told us

Numbers from X3 (UC8253/UC8279) and X4 Pro (probed to UC8179), 2026-09-21.

| | X3 | X4 Pro |
|---|---|---|
| indicator push | 25–26 ms | 54–112 ms |
| extra drain forced on the destination | ~260–330 ms | ~495 ms (FAST) / ~1269 ms (HALF) |
| destination refresh alone | 435–1067 ms | ~1500–1700 ms |

Worst case measured: `reader -> Home` on X4 Pro took **2977 ms** with the indicator
(waits 1269 + 1493) against ~1700 ms without. The indicator nearly doubled it.

**The premise was wrong.** Async overlaps the indicator's waveform with the transition's *CPU*
work, but the destination's own refresh still has to drain it — `displayBuffer()` opens with
`syncPendingAsync()`. Measured CPU work is 50–350 ms against a 500–1300 ms waveform, so we mostly
serialise two refreshes instead of hiding one.

A premise that also collapsed: "Settings is a cheap transition not worth marking". Entering
Settings costs **1067 ms** on X3 — the panel, not the work. At that scale marking it is more
justified, not less. That is the argument for *not* building the prediction model yet.

## What we learned

**1. `displayWindow()` means two different things.** Only `Ssd1677Driver` and `PaperMonoDriver`
override it; the other ten inherit a base that does a whole-panel `RefreshMode::Fast`. Nothing
lets a caller tell which. Written up in `display-capability-audit-2026-08-17.md`.

**2. The UC81xx drivers can do real partial windows and don't expose it.** `Uc8179Driver` and
`Uc8253X3Driver` already define and drive `0x90` PTL / `0x91` PTIN / `0x92` PTOUT. `Uc8179Driver`
even enters partial mode on its FAST path and then deliberately uses the full panel
("PTIN — whole-panel partial (no 0x90 window)"). The gap is an unimplemented override, not
hardware.

**3. Everything we needed already existed in the tree.** Three for three:
- `BaseTheme::drawPopup()` + `PopupShip` — the mechanism, already used by `SleepActivity` as a
  busy indicator ("Entering sleep").
- The `PopupShip::Async` contract already documented the X3 sleep bug we then hit: the caller
  owes a `finishDisplayAsync()` before anything "frees a framebuffer —
  `releaseSecondaryBuffer()` in particular does NOT drain a refresh in flight, and X3 re-reads
  the frame after the waveform."
- `GfxRenderer::hasRefreshOverridePending()` — a peek-don't-consume query whose documentation
  names this exact caller: "a caller that is about to issue an intermediate refresh (which would
  consume the override)".

The expensive part of this work was not reading first.

**4. `GfxRenderer::drawImage()` does not rotate bitmap content** — there is a literal
`// TODO: Rotate bits`. It rotates position only, so any bitmap icon lies on its side when the UI
is not in the panel's native orientation. This has stayed invisible because the existing
`LoadingIcon` is diagonal dots, which is nearly rotation-invariant. Our hourglass is drawn with
`drawLine()` for this reason.

**5. T5S3 partial updates do not save waveform time.** `Panel_EPD::display()` carries a sub-rect
through its queue, but the output loop scans the full panel every frame:
`for (uint_fast16_t y = 0; y < mh; y++) { blit_dmabuf(...); bus->writeScanLine(...); }`. A smaller
rect only shrinks the CPU staging loop. This matches the measured rule in
`lilygo-t5s3-refresh-audit-2026-09-15.md` (bank length x ~33 ms, independent of pixels changed).
LovyanGFX guides written for LCD boards (`pushBufferDMA`, `waitDMA`, `setWindow`) do not transfer
to this panel class.

**6. T5S3 async is available and unused.** `Panel_EPD` already runs refreshes on its own task via
`_update_queue_handle`; `waitDisplay()` is the join, and `pushCanvas()` already joins before the
next push. `LgfxEpdDriver` throws the overlap away by calling `settleDisplay()` after every push.
The reason is a real race, documented at `LgfxEpdDriver.cpp:483-494`: `display()` raises
`_display_busy`, yields, and only then posts, so a caller that trusts the flag tears the panel
task's diff copy — which has frozen a reader before.

## Bugs found on device, and what fixed them

| Symptom | Root cause | Fix |
|---|---|---|
| X3: sleep cover drawn over home screen + hourglass | `renderOverlaySleepScreen()` composites onto the write buffer with no `clearScreen`; our sync+swap changed what was in it | `SleepActivity::suppressesBusyIndicator()` |
| X4 Pro: hourglass flashed when leaving the frontlight drawer | a Pop has no `pendingActivity`, so only a source-side check catches it | check `suppressesBusyIndicator()` on both ends |
| Hourglass lay on its side | `drawImage()` never rotates bits | draw with `drawLine()` instead of a bitmap |
| Indicator went out as `HALF` (112 ms push, 1269 ms drain) | `triggerDisplayAsync()` calls `consumeRefreshOverride()`, eating a clean queued for the destination | **not fixed** — see below |

## What still needs testing

Nothing below has been checked on device since the theme refactor.

- All three: hourglass upright, screen-centred, correct in all four orientations.
- X3: power off from Home — the sleep cover must be clean.
- X4 Pro: frontlight drawer — no flash on open *or* close, and it must open over the page you
  were actually reading.
- T5S3: completely untested. Expect `(blocking)` and a full refresh cost.
- Whether the extra `DRF` wait disappears with the setting off. That is the clean A/B for the
  "destination drains the indicator" reading, which is currently inferred from the wait pattern
  rather than measured directly.

## What remains to be done

In the order that makes sense:

1. **Stop consuming the refresh override.** One line in `showBusyIndicator()`:
   `if (renderer.hasRefreshOverridePending()) return;`. Kills the 1269 ms case outright and stops
   the ghosting-budget theft. Independent of everything else.
2. **Make `releaseSecondaryBuffer()` drain a pending refresh**, as `borrowSecondaryBuffer()`
   already does. Retires the whole "caller owes a `finishDisplayAsync()`" hazard class at its
   root, and would let `SleepActivity` drop its arm-after-release ordering workaround.
3. **Implement `displayWindow()` on `Uc8179Driver` and `Uc8253X3Driver`** using the `0x90` window
   they already drive, plus `PanelDriver::supportsWindowedRefresh()` surfaced through the facade,
   and the stale-`frameBufferActive` fix from `secondary-buffer-management.md`. Snapping belongs
   in `GfxRenderer::displayWindow()` because the constrained logical axis flips with orientation.
4. **Add `PopupShip::Window`**, shipping only the `Rect` that `drawPopup()`/`drawBusyIndicator()`
   already return. At that point the indicator is a small partial, most of the suppression logic
   stops being load-bearing, and **every popup in the app gets cheaper** — including the
   "Entering sleep" one that today pays a full-frame async refresh.
5. **T5S3 async**: `displayStart()`/`displayFinish()` in `LgfxEpdDriver` around `Panel_EPD`'s
   existing task, preserving the yield-before-wait discipline. Needs the hardware to validate.

Items 3 and 4 are the point. Until then the indicator is paying a full-panel waveform for a
62x62 box, and no amount of tuning changes that.

## Recommendation

Do not merge the indicator on its own. Items 1–4 stand on their own merits — the frontlight
drawer, the status bar and `BookInfoActivity` all want a real partial refresh — and the indicator
should land as a small consumer of that infrastructure rather than as the thing justifying it.
