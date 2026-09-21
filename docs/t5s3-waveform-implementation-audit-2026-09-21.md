# ED047TC2 waveform: vendor data vs LovyanGFX vs epdiy — audit, 2026-09-21

Follow-up to `lilygo-t5s3-refresh-audit-2026-09-15.md`, which found that LovyanGFX cannot
express the vendor's source-dependent waveform and worked around it. This audit reads the third
source — epdiy — to establish whether that is a hardware constraint or a library one.

**It is a library one.** epdiy consumes the same vendor blob in its native source x destination
form. LovyanGFX is the sole outlier.

## Sources read

| Source | Location |
|---|---|
| Vendor blob (decoded) | `freeink-sdk/tools/gen_ed047tc2_waveform.py`, which asserts its structure |
| Our generated bank | `freeink-sdk/libs/hardware/BoardT5S3/src/ED047TC2Waveform.cpp` |
| LovyanGFX `Panel_EPD` | `M5GFX/src/lgfx/v1/platforms/esp32/Panel_EPD.cpp` (in libdeps) |
| epdiy | `github.com/vroland/epdiy`, `src/output_common/lut.c`, `src/waveforms/epdiy_ED047TC2.h` |

epdiy is **not** vendored in this project. M5GFX ships a `Panel_EPDiy` wrapper class, but epdiy
itself is not pulled, so that path does not compile for us today.

## 1. Our decoder is independently confirmed

The existing audit claims "DU is exactly `L[15]` frames of full drive". epdiy's published phase
counts for the same blob, mode 1 (DU), against our decoded `L[15]`:

| blob range | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|
| epdiy DU phases | 25 | 22 | 22 | 22 | 18 | 17 | 15 |
| our `L[15]` | 24 | 21 | 21 | 21 | 17 | 16 | 14 |

Exactly one apart across all seven ranges (epdiy counts a terminating phase we do not). Two
independent decoders agreeing to within a constant is strong evidence both read the blob
correctly. **Our DU/fast path is optimal and needs no work.**

## 2. epdiy is source-aware; LovyanGFX is not

epdiy, `lut.c`:

```c
const uint8_t* p_lut = phases->luts + (16 * 4 * frame);
for (uint8_t to = 0; to < 16; to++) {
    for (uint8_t from_packed = 0; from_packed < 4; from_packed++) {
        uint8_t index = (to << 4) | (from_packed * 4);
```

A full 256-entry `[to][from]` table per frame — the vendor structure verbatim.

LovyanGFX, `Panel_EPD.cpp`:

```cpp
dst[lindex] = (((lu >> ((lv >> 4) << 1)) & 3) << 2) + ((lu >> ((lv & 15) << 1)) & 3);
```

`lv` holds two 4-bit pixel values used directly as indices. No source term anywhere.

epdiy offers three strategies, and the third names the gap precisely:

- `build_2ppB_lut_1k` — `[to][from]`, one pixel at a time
- `build_2ppB_lut_64k` — two pixels at once, fully source-aware
- **`build_2ppB_lut_64k_static_from`** — source assumed fixed

**LovyanGFX's design is structurally epdiy's `static_from` mode, permanently.** It is one point on
a spectrum epdiy spans, not an oversight.

## 3. What the flattening costs

Our `clean_rows()` substitutes saturation for source knowledge: every pixel drives to the black
rail for `L[15]` frames, then the white rail for `L[15]`, then descends — `3 x L[15]` frames
regardless of where the pixel started. Against the vendor's GC16 (mode 2):

| blob range | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|
| ours (`3 x L[15]`) | 72 | 63 | 63 | 63 | 51 | 48 | 42 |
| vendor GC16 | 46 | 43 | 40 | 38 | 38 | 44 | 57 |
| delta | +57% | +47% | +58% | +66% | +34% | +9% | **-26%** |

At ~33 ms/frame that is **0.8-0.9 s of avoidable time per clean refresh** across most of the
range, on an operation measured at ~2766 ms.

The loss is confined to the clean/GC16 path. DU is already optimal (§1), so page turns are
unaffected.

## 4. An anomaly worth chasing

At blob range 11 our substitute runs **fewer** frames than the vendor GC16 (42 vs 57). That is not
a win — it suggests we **under-drive** at that range, which is a quality and ghosting risk rather
than a speed one.

Note also that vendor GC16 is non-monotonic across temperature (46, 43, 40, 38, 38, 44, 57) while
DU falls monotonically (25 -> 15). Our generator's self-normalising substitute is built on
`L[15]`, which follows the DU curve, so it cannot track a GC16 curve that turns back up. Whatever
the vendor knows about ranges 10-11 is not captured by `L[15]` alone.

This is a candidate explanation for residual ghosting items still open in the 2026-09-15 audit.

## 5. Why teaching LovyanGFX source-awareness is expensive

Not a patch. Three structural obstacles, all in the hottest path:

**No spare index bits.** `_step_framebuf` entries are `int16` used directly as LUT indices:
bits 0-7 carry the two destination pixel levels, bits 8-14 the bank/phase selector, bit 15 the
"still has work" flag (`blit_dmabuf` branches on sign). A source-aware two-pixel lookup needs
8 bits of destination plus 8 of source — the entire word, leaving nothing for the bank or the
flag.

**LUT size.** `_lut_2pixel` is `lut_total_step * 256 * sizeof(uint16_t)`. Source-aware two-pixel
lookup is 64k entries, i.e. 128 KB per step — and it is read in the inner loop, so it must live
in internal RAM, not PSRAM.

**Hand-written assembly.** `blit_dmabuf` is Xtensa asm keyed to exactly that index format and
sign-bit convention. Changing the index layout means rewriting it.

**And a missing buffer.** Source-awareness needs the pixel's *previous displayed* level. `_buf`
holds only the target. LovyanGFX has no previous-frame plane; epdiy does.

## 6. Options

1. **Keep the substitute, fix range 11 only.** Smallest change. Addresses the under-drive, which
   is a correctness issue, and ignores the speed one. Does not need epdiy.
2. **Port source-awareness into `Panel_EPD`.** Biggest win (0.8-0.9 s per clean) but requires all
   four items in §5, including rewriting the asm inner loop. Upstreamable in principle.
3. **Move to the epdiy backend** M5GFX already wraps. Gets source-awareness and area-limited
   updates at once, but abandons our waveform generator, the clean/fast bank model and everything
   the 2026-09-15 audit established. A port, not a swap.

Option 1 is the only one that is cheap. Option 2 is the only one that keeps our waveform work and
fixes the speed. Nothing here has been measured on hardware; the frame counts are read from the
vendor tables and from source, not from a panel.

## 7. Separately: area-limited updates

`Panel_EPD::display(x, y, w, h)` accumulates a dirty rect and posts it through
`_update_queue_handle`, but the output loop scans the full panel every frame:

```cpp
for (uint_fast16_t y = 0; y < mh; y++) { blit_dmabuf(...); bus->writeScanLine(...); }
```

The rect only shrinks the CPU staging loop, not waveform time. `Panel_EPDiy::display()` by
contrast builds an `EpdRect` and hands it to epdiy, which honours it. So partial refresh is a
second capability available on the epdiy path and not on ours — relevant to any feature wanting a
cheap small repaint (busy indicator, frontlight drawer, status bar).

LovyanGFX guides written for LCD boards (`pushBufferDMA`, `waitDMA`, `setWindow`) do not transfer:
those are the `Panel_LCD` path, and this is `Panel_EPD`.

## 8. What is cheap and unrelated to all of the above

`Panel_EPD` already runs refreshes on its own task via `_update_queue_handle`, with
`waitDisplay()` as the join. `LgfxEpdDriver` discards that overlap by calling `settleDisplay()`
after every push. Implementing `displayStart()`/`displayFinish()` there needs no waveform work and
no backend change — only the yield-before-wait discipline documented at
`LgfxEpdDriver.cpp:483-494`, which exists because trusting `_display_busy` too early has frozen a
reader before.
