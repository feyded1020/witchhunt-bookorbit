# LilyGo T5 S3 refresh logic — audit

**Date:** 2026-09-15
**Scope:** the whole path from the ED047TC2 vendor waveform to a refresh on the glass —
`gen_ed047tc2_waveform.py` → `ED047TC2Waveform.cpp` → `LilyGoT5S3LgfxConfig.cpp` →
LovyanGFX `Panel_EPD` → `LgfxEpdDriver` → `HalDisplay` → the activities.
**Reason:** three rounds of refresh patches with changing and incomplete results. The patches were
treating symptoms; this establishes the mechanism.

Every claim below is either read out of the vendor blob's decoder, read out of the LovyanGFX source
in `.pio/libdeps/lilygo_t5s3/M5GFX`, or computed from the generated tables. Where something is
unverified it says so.

---

## 1. What the vendor waveform actually is

The reference is the ED047TC2 waveform blob LilyGo ships as `Waveform_header/ED047TC2.h`
(epdiy: `epdiy_ED047TC2.h` — the same data). `tools/gen_ed047tc2_waveform.py` decodes it and
asserts its structure before emitting anything.

Its shape:

- Three draw modes — **DU** (1), **GC16** (2), **GL16** (5) — across **7 temperature ranges**
  (blob ranges 5..11).
- Each table is `[phases][16][4]`: a 2-bit drive code for every **(destination level, source
  level)** pair, per phase. `0` = no drive, `1` = toward black, `2` = toward white.
- The generator proves the waveform is a **separable impulse waveform**:
  `net_frames(to, from) == L[to] − L[from]`, with a zero diagonal and a monotonic `L` per range.
- **DU is exactly `L[15]` frames of full drive** for a rail-to-rail transition, and nothing else.

The per-range drive length `L[15]` — the only number the vendor waveform really reduces to:

| range | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `L[15]` frames | 24 | 21 | 21 | 21 | 17 | 16 | 14 |

**The vendor model is source-aware.** How far to drive depends on where the pixel *is*, not only
on where it is going. Hold on to that.

## 2. What LovyanGFX can express — and the mismatch

`Panel_EPD` expands each configured LUT into `_lut_2pixel`. The expansion
(`Panel_EPD.cpp:277-284`) reads one 32-bit word per phase and indexes it by **level alone**:

```cpp
dst[lindex] = (((lu >> ((lv >> 4) << 1)) & 3) << 2) + ((lu >> ((lv & 15) << 1)) & 3);
```

`lv` is a byte holding two 4-bit pixel values, and each is used directly as the index into the
16 codes packed in `lu`. There is **no source term anywhere**. A LovyanGFX LUT column is indexed
by destination only.

So the vendor's source×destination waveform cannot be represented. Our generator's answer is to
make every column **self-normalizing**: saturate at a rail first — the clamp erases whatever state
the pixel arrived in, doing the job a source index would — then walk to the target.

- `fast_rows()` — DU rails verbatim, plus one column per AA grey that spends `L[15]` frames at the
  white rail and then descends.
- `clean_rows()` — a GC16 substitute: `L[15]` frames to black, `L[15]` to white, then descend to
  each level. Three phases, uniform across all 16 columns.

That adaptation is sound, and it is what lets a page carry its anti-aliasing greys in the same push
as its text. **But it has a consequence nobody carried forward**, and it is the root of everything
below.

## 3. The root cause: bank identity *is* the diff key

`Panel_EPD` tracks per-pixel refresh progress in `_step_framebuf` as `(lut_block << 8) | level`,
two uint16 words per pixel pair: `d[0]` the active waveform position, `d[1]` the reserved request.
A push writes `d[1] = s0` where

```cpp
uint_fast16_t lut_offset = me->_lut_offset_table[new_data.mode] << 8;
if (flg_fast) { lut_offset += 0x8000; }
...
s0 = s[0] + lut_offset;
```

`s0` therefore carries **the bank's own LUT offset** in its high byte. Both write paths then decide
whether to drive by comparing against it:

| mode | test | drives |
| --- | --- | --- |
| `epd_fast`, `epd_fastest` | `d1 != s0` | any pixel whose request differs |
| `epd_text` | `white != d1 \|\| d1 != s0` | every non-white pixel, plus any whose request differs |
| `epd_quality`, eraser | `d1 != s0` | any pixel whose request differs |

Because the offset is part of the comparison, **the bank a pixel was last driven under is part of
its identity**. Two consequences follow, and together they explain every symptom:

**(i) Same bank twice ⇒ a genuine differential update.** Only changed pixels are driven. That is
correct and desirable for page turns.

**(ii) A different bank ⇒ every pixel is re-driven, full screen.** Nothing compares equal, because
the offsets differ.

So on this panel a refresh is a scrub **if and only if** it uses the clean bank *and* the previous
refresh used a different one. "Which waveform" and "how much of the screen" are not independent
choices — they are the same choice. Every refresh policy question on this board reduces to this
table:

| previous bank | this push | what actually happens |
| --- | --- | --- |
| fast | clean | **full-screen GC16 — a real scrub** |
| clean | clean | only the ink is driven; the white background is skipped → both pages at once, then an imprint |
| clean | fast | **full-screen DU** — moves ink to target, scrubs nothing → ghosting |
| fast | fast | true differential DU — correct for page turns |

## 3a. Confirmed on hardware

Device log, 2026-09-15, panel at 24 °C (waveform range 3), reader page turns with
`refreshFrequency = 19`. Boot line and per-refresh durations both match the model.

**Boot line — three independent numbers, all matching the computed tables:**

```
[epd] panel 24 C -> waveform range 3 (24..27 C), 21 drive frames, AA greys at level 7/12
[epd] LUT budget 105/128 blocks (...
```

| reported | computed (§1, §6.1) |
| --- | --- |
| range 3 (24..27 °C) | range 3 |
| 21 drive frames | `L[15] = 21` |
| AA greys at level 7/12 | dark 7, light 12 |
| 105/128 blocks | total 105, last block 104 |

**Refresh durations — the bank is measurable without any trace build.** Time from the
`[RCY] triggerWithRefreshCycle:` line to the page-summary line that follows it:

| mode | measured | n |
| --- | --- | --- |
| `fast` | 1376-1410 ms, mean **1397 ms** | 7 |
| `HALF` | **2624 ms** | 1 |

Ratio **1.88×**, against a predicted 68/35 = **1.94×** (clean bank 64 rows + 4-row eraser, versus
the 35-row fast bank). Subtracting the ~120 ms sprite copy that is flat in every push leaves
~1275 ms and ~2500 ms, i.e. ~35 and ~76 blocks at 33 ms/frame.

So **duration alone identifies the bank at range 3**: roughly 1400 ms is the differential bank,
roughly 2600 ms is the clean bank. That makes `[DISP] #N displayBuffer mode=X took N ms`
(LOG_LEVEL=2, emitted for every push outside the reader) a sufficient diagnostic; the
`LGFX_EPD_PUSH_TRACE` build is only needed to measure the re-tag pass of §5 directly.

This HALF landed on the clean bank correctly, because inside the reader a periodic scrub is always
preceded by fast page turns. That is consistent with symptom (a) reading as fixed.

## 4. Tracing the three reported symptoms

**(a) "Half and Fast were indistinguishable."** `halfUsesFastBank = true` mapped `Half` to
`epd_fast` — the same bank `Fast` uses. Row 4 of the table, always. The board had no mid-tier
refresh at all, and nothing scrubbed until something asked for `Full` by name.

**(b) "Exiting the reader ghosts."** Partly the downgrade, but the device log shows the first
explanation cannot be the whole story. The reader's periodic scrub *is* a `Half`
([ReaderUtils.h:226](../src/activities/reader/ReaderUtils.h#L226)) and `enforceExitFullRefresh()`
arms another for the screen after it, so exiting right after a periodic scrub gives row 3 — a
full-screen **DU**, exactly the waveform that leaves residue. But the log reports
`freq=19`, so that coincidence is about 1 exit in 19, and the symptom is reported as more frequent
than that.

The likelier companion cause needs no downgrade at all: **a plain `Fast` push landing after a clean
one is also row 3.** Home consumes the armed `Half` on its first paint, and any cover that has to
be decoded repaints Home afterwards through `renderer.displayBuffer()` with the default `Fast`
([HomeActivity.cpp:972](../src/activities/home/HomeActivity.cpp#L972); the unconditional second
render was removed, but `yieldAfterDecode()` still calls `requestUpdate()` per decoded cover). That
second paint is a full-screen DU over a freshly scrubbed panel.

**Not fixed by §5**, which only governs clean pushes. Needs the exit log to confirm before
anything is changed — see §10.

**(c) "After Screen Repair, ghosting is very apparent." — MEASURED, and my first explanation was
wrong.**
[ScreenRepairActivity](../src/activities/settings/ScreenRepairActivity.cpp#L50) is **thirteen
consecutive `Half` pushes** (`kAlternations = 5` pairs + `kTrailingWhite = 3`), and its own comment
states the requirement: *"HALF is the clean bank, the one waveform on this board that drives every
pixel every time, which is the whole exercise."*

I predicted the downgrade would send every second push through the DU bank. **The device says
otherwise — all thirteen took the clean bank:**

```
#7  HALF 2579 ms   <- follows FAST #6, no re-tag needed
#8  HALF 2765 ms
#9 ..#19 HALF 2766-2768 ms   <- rock steady, none near 1400 ms
```

So the downgrade does not fire in the repair cycle at all, and the alternation claim is
**falsified**. Note instead the **+189 ms step from #7 to #8**, appearing exactly when the previous
push was clean: that is a re-tag pass, and its size says it is the cheap one of §5, not a flash.

The real cause is the three lines at the end of the cycle:

```
#19 HALF 2768 ms   repair ends            -- clean bank
#20 FULL 3791 ms   DONE screen            -- clean bank
#21 FAST 1558 ms   Back -> Settings       <- full-screen DU over a freshly scrubbed panel
```

**`#21` is row 3 of the table with no downgrade involved** — a plain `Fast` push landing after a
clean one, which re-drives every pixel through the DU bank and scrubs nothing, so the DONE screen
ghosts through into Settings. This would happen on unmodified upstream code too. It is the
`Fast`-after-clean case, and it is now measured rather than hypothesised.

**`#20` resolved by the trace — not a second refresh.** It makes exactly ONE push and uses the same
`fastest` re-tag as every other clean push. The extra time is inside the clean push's own settle:

```
#19 HALF 2766 ms  sprite=115 settle=2409  normalize: re-tag through fastest
#20 FULL 3606 ms  sprite=137 settle=3176  normalize: re-tag through fastest
```

Over the same 68 blocks that is 35.4 ms/frame for the blank repair screens versus 46.7 ms/frame for
the content-rich DONE screen. So frame cost is mildly content-dependent after all -- the earlier
"duration is independent of how many pixels changed" rule holds only to within about 30%. Second
order, no correctness impact; noted rather than chased.

All three are the same fault: **the refresh policy was choosing banks without accounting for the
fact that the bank choice also decides the update's extent.** My "the fallback almost never fires"
claim was wrong; (b) and (c) are where it fires most.

## 4a. Verified on hardware with the bank trace

`env:lilygo_t5s3_pushtrace`, 2026-09-15, panel at 24 C (range 3). Every push named by bank:

| push | requested | bank actually used | re-tag |
| --- | --- | --- | --- |
| #1 Boot | HALF | `text(clean bank, eraser)` | none (boot state is fast) |
| #2 Home | FAST | `fast(diff bank)` | - |
| #3-#6 Settings, ScreenRepair | FAST | `fast(diff bank)` | - |
| **#7 repair** | HALF | `text(clean bank, eraser)` | none (follows FAST) |
| **#8-#19 repair** | HALF | `text(clean bank, eraser)` **x12** | `fastest` on every one |
| #20 DONE screen | FULL | `text(clean bank, eraser)` | `fastest` |
| **#21 back to Settings** | FAST | `fast(diff bank)` | - |

**The re-tag is confirmed by name and measured.** `#7` (no re-tag) takes 2579 ms; `#8` (re-tag)
takes 2766 ms, with *identical* `sprite=115 settle=2409` -- so the whole 187 ms delta is the re-tag,
sitting outside the traced push exactly as designed.

**187 ms, against 1239 ms** (35 blocks x 35.4 ms) for the white-flash fallback: the no-drive bank is
**6.6x cheaper**, and it is invisible on the glass. The `blit_dmabuf` derivation in §5 holds.

**All three symptoms are fixed on this build**, user-confirmed on device: (a) Half and Fast are
distinct again, (b) "no visible ghosting after epub reader exit", (c) "no ghosting buildup after
exiting from repair".

`#21` -- a `fast(diff bank)` push landing directly on the clean `#20` -- therefore **exists but is
not a defect**. The differential bank runs the vendor DU rails for the full L[15] frames, so every
drive saturates and lands from any source state; re-driving an already-correct pixel is a redundant
impulse, not a visible artefact. The row-3 entry in the §3 table describes a *missing scrub*, which
matters only where a scrub was expected -- and after `#20` has just scrubbed, nothing is expected.

**So no change is warranted for it.** Recorded here because it was predicted, measured, and then
found harmless: that is worth knowing the next time the table tempts someone into a fix.

## 5. The fix that follows from the mechanism

`Half` and `Full` must both take the clean bank, always — they are the modes whose purpose is to
scrub. The only thing needed is to guarantee the *previous* bank differs. There is no budget for a
second clean bank (§6), so the bank change has to be made without a waveform.

`blit_dmabuf` supplies exactly that instrument (`Panel_EPD.cpp:807-876`):

```cpp
if (s_0 >= 0) {
  tmp = lut[s_0];
  s_0 += 256;
  buf += tmp << 4;
  if (tmp == 0) { s_0 = src[1]; tmp = s_0 | 0x8000; src[1] = tmp; }
  src[0] = s_0;
}
```

A **zero LUT entry means "this pixel is finished"** — load the reserved request, mark idle, drive
nothing. A bank that is a single `0u` word is therefore a complete refresh that ends after one
frame and never touches the ink, **while still stamping its own offset into `d[1]`**. That is the
whole of what the clean bank needs in order to stop skipping the background.

It must be `epd_fastest`, not `epd_quality`, even though both slots are spare here: the fast modes
take the `flg_fast` branch, which writes `d[0] = s0 - 0x8000` and goes straight to the bank, while
the others prepend `lut_eraser` — the pixel would run the eraser's nudge and then stop with nothing
driving it home. That is exactly the half-inverted frozen screen seen when `Full` was once routed
to `epd_quality`; it was never a mystery, it is this.

Cost: one frame plus a canvas copy (~150 ms) instead of a full ~1150 ms flash. The board already
stubs `lutFastest` with `{0u}`, so the driver can **derive** this rather than take a new flag.

## 6. Defects found along the way

### 6.1 The generator's LUT budget assertions are wrong, in the unsafe direction

The real limit is hard: `blit_dmabuf` reads the progress word through `(int16_t)` and skips the
pixel when it is negative, so **no block index may reach 128** — every bank together, terminators
included, must fit in 128 rows. Overrun is silent and global: refreshes stop completing and the
panel looks dead.

`tools/gen_ed047tc2_waveform.py` gets this wrong three ways:

| constant | value | actual | effect |
| --- | --- | --- | --- |
| `LUT_ROW_BUDGET` | 255 | **128** | ~2× too permissive; will pass a waveform that kills the display |
| `ERASER_ROWS` | 3 | **4** | `lut_eraser` is 2 drive rows + a `~0u` no-op + the `0u` terminator (`Panel_EPD.cpp:151-159`); every total and `fast_start` is computed one low |
| `FAST_START_BUDGET` | 127 | correct but **incomplete** | it bounds where the fast bank *starts*, never where the last bank *ends* |

The 255 figure looks like it came from `_lut_offset_table` being `uint8_t[6]`
(`Panel_EPD.hpp:123`). That bounds the *offset type*, not the usable range; the signed progress
read is what binds.

Measured totals from the emitted tables (eraser 4 + stub 1 + clean + fast + stub 1):

| range | clean | fast | total | last block | headroom to 128 |
| --- | --- | --- | --- | --- | --- |
| **0 (coldest)** | 73 | 40 | **119** | 118 | **9** |
| 1 | 64 | 35 | 105 | 104 | 23 |
| 2 | 64 | 35 | 105 | 104 | 23 |
| 3 | 64 | 35 | 105 | 104 | 23 |
| 4 | 52 | 29 | 87 | 86 | 41 |
| 5 | 49 | 27 | 82 | 81 | 46 |
| 6 | 43 | 23 | 72 | 71 | 56 |

Range 3 matches the device's own boot line (`105/128 blocks (23 spare)`), so the model is confirmed
against hardware. **The coldest range is nine rows from silent failure**, and a fourth clean phase
(+24) would break every range below ~27 °C. This is the failure that once blanked the panel when
cool; the generator as it stands would not have caught it, and still would not.

### 6.2 `_lut_remain_table` is dead

Assigned at `Panel_EPD.cpp:273` and referenced only in two commented-out log lines. Nothing reads
it. Worth knowing before anyone reasons about bank lengths from it.

### 6.3 A broken string literal hidden behind a disabled `#define`

The trace line added to `normalizeForCleanBank()` in an earlier patch contained a **literal
newline** inside a string literal. It compiles only because `LGFX_EPD_PUSH_TRACE` is off in the
normal env — meaning `env:lilygo_t5s3_pushtrace`, the one env you would reach for to diagnose a
refresh problem, was the only one it broke. Fixed. **Both trace envs need building after any
driver change**, or diagnostics rot silently.

## 7. Unverified items worth settling

Neither is implicated in (a)/(b)/(c) — those are fully explained by §3 — but both are real
candidates for *residual* ghosting that survives a correct waveform.

### 7.1 VCOM is a copied default, never read off this panel

`LilyGoT5S3LgfxConfig.cpp:8` sets `kDefaultVcomMv = -1600`, written to the TPS65185. E Ink panels
carry a **per-panel** VCOM value printed on the FPC tail; a wrong VCOM leaves a standing DC offset,
which shows up as exactly the residual-image behaviour being chased here, and which no waveform can
fully scrub. −1600 mV is the epdiy/LilyGo reference default, not a measurement of *this* panel.

**Test:** read the value printed on the FPC and compare. If it differs by more than ~100 mV, set it
and re-test the ghosting.

### 7.2 The waveform is chosen once, at boot

`buildConfig()` samples the PMIC thermistor and picks a range; LovyanGFX expands the LUTs at panel
init and never re-reads them, so **the waveform tracks the temperature at boot, not the temperature
now**. Ranges are ~3 °C wide and the drive length changes by up to 3 frames between neighbours, so
a panel that warms during a long session (frontlight, ambient, body heat) ends up over-driven —
which reads as ghosting of the opposite polarity.

**Test:** log the panel temperature at boot and again after 30+ minutes of reading. If the range
index moves, re-selection on wake is worth doing; a reboot already re-picks it.

## 8. A note on the cost that was accepted

Making `Half` a clean-bank refresh means the bank alternates on almost every transition, so the
`Fast` push that follows a `Half` is a **full-screen DU** rather than a differential one. It costs
no extra time — refresh duration is bank length × ~33 ms and is independent of how many pixels
changed — but it is a full-screen impulse where the vendor model expects a differential one, and DU
is not DC-balanced. The periodic clean refresh is the vendor-sanctioned remedy for exactly that, and
it now actually runs, so this is a fair trade rather than a free one. Worth re-checking if
long-session ghosting persists after §7.1.

## 9. What changes

**This repo / SDK fork**

1. `epdModeFor()` returns the clean bank for both `Half` and `Full`; no downgrade.
   *(done, device-verified -- §4a)*
2. `normalizeForCleanBank()` re-tags through the no-drive bank when one exists, falling back to a
   white flash through the differential bank otherwise. *(done, measured at 187 ms -- §4a)*
3. Fix `gen_ed047tc2_waveform.py`: `LUT_ROW_BUDGET = 128`, `ERASER_ROWS = 4`, and assert the **last
   bank's end** rather than only the fast bank's start. *(§6.1 — done)*

**Upstream (Free-Ink/freeink-sdk)**

- **#101 must be rewritten.** It currently proposes the downgrade that causes (b) and (c). The
  replacement is §3 + §5: never downgrade, re-tag instead.
- **#102 stands, and is strengthened by §6.1** — the runtime check catches precisely what the
  generator's own assertions miss. Its constants (128, eraser 4) are the correct ones.
- #103 and #74 are untouched by this audit.

## 10. Closed

Everything this audit opened is now measured. Nothing is outstanding against the three symptoms.

**Answered:**

* The downgrade never fires in the repair cycle; all thirteen `Half` pushes take the clean bank
  (§4a, by bank name).
* The re-tag costs **187 ms** through the no-drive bank, 6.6x cheaper than the white flash, and is
  invisible on the glass (§4a).
* `#20 FULL` is one push with a longer settle, not a second refresh (§4c).
* `Fast`-after-clean is real, measured, and harmless (§4a).

**Deliberately not chased, with reasons:**

* Frame cost is mildly content-dependent (35.4 vs 46.7 ms/frame over the same 68 blocks). Second
  order, no correctness impact.
* `#21` needs no fix -- see §4a.
* The `lilygo_t5s3` env failed to link once while `pushtrace` succeeded from the same tree, and
  cleared on rebuild. It was **not** the zero-byte-object trap: a scan found no empty `.o` in the
  build tree. Cause unestablished; worth chasing only if it recurs.

**Still unverified, and unrelated to the three symptoms** (both from §7, both candidates for
residual ghosting that survives a correct waveform):

* VCOM is the epdiy reference default, never read off this panel's FPC.
* The waveform is chosen once at boot and does not track temperature drift during a session.

## 11. What the discipline cost, and saved

Three rounds of patching produced changing results because each round reasoned from a model that was
never checked against the panel. The model in §3 -- *bank identity is the diff key, so "which
waveform" and "how much of the screen" are the same choice* -- explains all three symptoms, and
every number it predicts has now been confirmed on hardware: the LUT budget (105/128), the drive
length (21 frames), the grey levels (7/12), the clean/fast duration ratio (1.88x measured vs 1.94x
predicted), and the re-tag mechanism down to its cost.

Two predictions it produced were **wrong**, and both were caught by measurement rather than by
review: the repair cycle does not alternate banks (§4c), and `Fast`-after-clean does not actually
ghost (§4a). Both had been written up as fact before the device said otherwise.
