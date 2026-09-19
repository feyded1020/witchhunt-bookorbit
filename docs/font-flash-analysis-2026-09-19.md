# Font flash analysis, 2026-09-19

Where the firmware's font bytes actually go, which levers are still unpulled, and what is worth
taking from crosspoint-reader
[PR #3083](https://github.com/crosspoint-reader/crosspoint-reader/pull/3083) (GlyphStream).

Everything below is measured on this tree, not estimated, unless a row says otherwise. Method:
`riscv32-esp-elf-nm --print-size --size-sort --radix=d` over
`.pio/build/default/firmware.elf`, filtered to the font symbols; field ranges by parsing every
`lib/EpdFont/builtinFonts/*.h`.

Two baselines matter and only one of them can be taken from a host: the flash figures below are
measured, the **performance** figures need a device and are a table to fill in — see
[Performance baseline](#performance-baseline) for the harness and the procedure.

## Flash baseline

`pio run -e default`, after the larger-UI-font change landed:

```
RAM:   [==        ]  18.5% (used     60,496 bytes from   327,680)
Flash: [==========]  95.5% (used  6,259,707 bytes from 6,553,600)
```

Font data is **2,785,785 B — 44.5% of the image**, in five buckets:

| bucket | bytes | share of font data | state |
| --- | ---: | ---: | --- |
| glyph bitmaps | 1,622,859 | 58.3% | per-group DEFLATE (zopfli); the 1-bit UI faces are **uncompressed** |
| **glyph records** | **769,008** | **27.6%** | **uncompressed, 48,063 × 16 B — the unpulled lever** |
| kerning | 346,746 | 12.4% | already sparse CSR, 86.6% of dense entries were zero |
| intervals | 26,916 | 1.0% | small |
| group tables | 13,220 | 0.5% | small |

A 5% cut of font data is 139 KB. Lever 1 below is 384 KB.

## Already done — do not re-run these

- **Ring streaming** (`EpdFontGroup.ringBytes`, `FontDecompressor::GroupStream`). This is the
  RAM half of the problem PR #3083 set out to solve, and it is in the tree: a group streams
  through a ring sized to its longest actual back-reference, so the transient is ~4 KB instead
  of up to 64 KB. Cost was +2.11% flash.
- **i18n language packs** — took the image 96.7% → 93.1% for +24 B of RAM.
- **Sparse kerning** — 86.6% of dense matrix entries were zero; ~583 KB → ~165 KB.
- **Bitmap recompression survey.** brotli `q11 lgwin=16` is the only viable win at **−95 KB
  gross**, and the decode-only brotli build costs ~25–40 KB of flash, so net is **−55 to −70 KB**
  for a new decoder on the font path. zopfli tuning (−0.2% for 30× the encode time), lzma2
  (+1.7%), zstd (−0.8%), shared/trained preset dictionaries (+3.6% to +6.2%) and byte-delta or
  bitplane pre-transforms (+32%, +35%) are all dead. Per-glyph DEFLATE is dead at any dictionary
  size: best case was a shared 8 KB dictionary at **+41% flash**.

## Lever 1 — repack the glyph record, 16 B → 8 B: −384,504 B

`EpdGlyph` (`lib/EpdFont/EpdFontData.h:70-78`) is 16 bytes and half of that is recoverable.
Measured over all 48,063 shipped glyphs:

| field | today | measured range / fact | can be |
| --- | --- | --- | --- |
| `width` | `uint8_t` | max 61 | unchanged |
| `height` | `uint8_t` | max 46 | unchanged |
| `advanceX` | `uint16_t` | max 1000 (12.4 fp = 62.5 px) | unchanged |
| `left` | `int16_t` | **−22 .. 11** | `int8_t` |
| `top` | `int16_t` | **−4 .. 42** | `int8_t` |
| `dataLength` | `uint16_t` | **derivable for 48,063 / 48,063 glyphs** | *deleted* |
| `dataOffset` | `uint32_t` | **max 46,308** | `uint16_t` |
| *padding* | 2 B | forced by the `uint32_t` | *gone* |

`dataLength` is exactly `ceil(width * height * bpp / 8)` — bitmaps are packed as a continuous
bit stream, not row-aligned, so there is no stride to account for. This was verified against
every glyph in every shipped face, and against `fontconvert_sdcard.py:755`, which computes it
the same way for `.cpfont`.

The repacked record is naturally aligned with no padding, which matters on RISC-V:

```c
uint8_t  width;       // 0
uint8_t  height;      // 1
uint16_t advanceX;    // 2   (aligned)
int8_t   left;        // 4
int8_t   top;         // 5
uint16_t dataOffset;  // 6   (aligned)
                      // = 8 bytes
```

**48,063 × 8 B = 384,504 B**, which is 13.8% of font data and **6.1% of the whole image**. It
costs no decode CPU — it is strictly less flash to read per glyph and half the cache footprint
during layout, so if anything it is marginally faster. That is what makes it a better first move
than any compression scheme: every other lever trades CPU for bytes, and this one does not.

### The complication: `.cpfont` maps `EpdGlyph` in place

`SdCardFont.cpp:14` asserts `sizeof(EpdGlyph) == 16` "to match .cpfont file layout", and
`SdCardFont.cpp:1180` points `miniData.glyph` straight into the mmap'd file. So the struct is a
*file format*, and it cannot simply shrink.

The two must be split, which the codebase already does twice for exactly this reason — dense vs
sparse kerning, and packed vs split kern class maps, where whichever pointer is non-null selects
the representation (see the comments in `EpdFontData.h`). Built-in fonts get the 8-byte record;
`.cpfont` keeps its own.

Note that SD fonts could not use a `uint16_t dataOffset` anyway: theirs is a global offset into
an uncompressed per-style bitmap section that runs to ~170 KB, where the built-ins' is a
*within-group* offset bounded by the 64 KB group cap. That is why the split is not just a
compatibility concession — the two formats genuinely want different widths.

### Surface to change

45 field accesses outside `SdCardFont`: `GfxRenderer.cpp` (32), `FontDecompressor.cpp` (7),
`EpdFont.cpp` (5), `FontDecompressor.h` (1). The one design question is `EpdFont::getGlyph()`
(`lib/EpdFont/EpdFont.h:20`), which returns `const EpdGlyph*` — a pointer into storage that no
longer has that shape for built-in fonts.

Recommendation: change it to fill a caller-provided record,
`bool getGlyph(uint32_t cp, EpdGlyph& out) const`. A 16-byte stack local at each call site, the
built-in path widens its 8-byte record into it, the SD path copies. This is worth doing on its
own merits: today's contract is "valid until the next `glyphMissHandler` call that causes a
ring-buffer eviction", a lifetime rule that 32 call sites in `GfxRenderer.cpp` have to honour
and that nothing enforces. Filling an out-parameter deletes the hazard rather than extending it
to a second representation.

`isOverflowGlyph(const EpdGlyph*)` (`SdCardFont.cpp:1855`) keys on pointer identity and would
need a flag or index instead.

### The same lever on `.cpfont` — the "cpfont sizes" question directly

Measured on a real file: Noto Sans 14, four styles, `reading` intervals →
**743,382 B**, of which the glyph records are **112,448 B (15.1%)**.

`dataLength` and the `int8` narrowing apply unchanged; only `dataOffset` has to stay wide. At
`uint24` (`memcpy` on read — the RISC-V alignment rule in the dev guide applies) the record is
10 bytes:

| variant | record | saving on this file |
| --- | ---: | ---: |
| drop `dataLength`, narrow `left`/`top`, keep `uint32` offset | 12 B | 28,112 B (**3.8%**) |
| same, `uint24` offset | 10 B | 42,168 B (**5.7%**) |

So `.cpfont` clears the 5% bar, but only with the 3-byte offset. The cost is a format version
bump: `VERSION = 4` (`fontconvert_sdcard.py:880`) becomes 5, and every `.cpfont` already on a
user's card has to be regenerated. `build-sd-fonts.py` plus the in-firmware font download
manager make that cheap for the 26 hosted families; hand-converted fonts break. The reader
should keep accepting v4 rather than rejecting it — the loader can branch on the header version
the way the kerning paths branch on a null pointer.

**Suggested split:** do the built-in repack first, as its own PR. It is the larger win, it has no
compatibility story at all, and it settles the `getGlyph()` signature that the `.cpfont` change
would otherwise have to settle at the same time.

## Lever 2 — what to take from PR #3083, and what not to

PR #3083 stores every glyph as an independent range-coded stream with base-glyph references and
offline-trained context trees. It reports **−556 KB** of bitmap data (0.59×) on upstream's
corpus; scaled to our 1,622,859 B of bitmaps that would be roughly −650 KB. It has been a
**draft since 2026-08-18** and the author's stated blocker is not compression but caching.

**Do not port the codec.** Three reasons, in order of weight:

1. **It is unfinished where it matters to us.** Its rendering cost went +107% before caching, and
   the fix was a reader-scoped packed-glyph cache. The Home screen was left regressed — Uri
   measured cursor movement at **14 ms → 70 ms** — because that cache is released on leaving the
   reader, and the author converted the PR to draft rather than grow heap outside the reader. A
   second reviewer then could not reproduce the 385 ms figure at all (~720 ms).
2. **Our UI fonts cannot afford per-glyph decode, and that is structural here.** UI screens never
   prewarm: only the reader activities call `FontCacheManager::createPrewarmScope()`. A glyph the
   prewarm did not cover falls through to a 4-slot LRU
   (`FontDecompressor.cpp:270-366`, `FALLBACK_CACHE_SLOTS = 4`) and then streams its group. Four
   slots against ~30 distinct glyphs on a menu screen thrash on every repaint. This is why the
   1-bit Inter faces are stored uncompressed, and why the larger-UI-font work shipped a new Inter
   14 rather than reusing a compressed Noto Sans face.
3. **We already have the RAM half.** The group-size problem the PR opens with is the one
   `EpdFontGroup.ringBytes` solved here, so the remaining delta is flash-only — and lever 1 gets
   384 KB of it with no decoder, no trained model, no RAM, and no CPU.

**Do take these two, independently of any compression change:**

- **A packed-glyph cache that outlives one page.** This was the single biggest win in the PR —
  681 ms → 385 ms, with pages 2, 3 and 5 decoding *nothing* — and it is orthogonal to how the
  bitmaps are stored. It would speed up today's DEFLATE groups and today's SD fonts, where a
  thrashing glyph ring is already a measured multi-second cost on the font preview screen.
  If it is made global rather than reader-scoped it also removes the objection in point 2, which
  is what would put brotli or GlyphStream back on the table later.
**Checked and NOT applicable:** the PR's other big win was style-aware prewarming — upstream
appended every text run to one shared page string and kept only style counts, so each unique
glyph was decoded for regular, bold *and* italic. Our scan already keys on style:
`FontCacheManager::recordText()` accumulates into
`scanByFont_[fontId].textByStyle[style & 0x03]` (`FontCacheManager.cpp:87-91`) and
`endScanAndPrewarm()` warms each (font, style) pair with only its own glyphs. There is nothing
to reclaim here.

## Performance baseline

Flash is only half the ledger. Every lever above except lever 1 pays for bytes with glyph-path
CPU, and **crosspoint-reader#3083 stalled because nobody could measure that trade** — its author
asked twice for an on-device benchmark and got "currently we dont have anything like this". The
regression that actually blocked it was found by a reviewer eyeballing one `LOG_DBG` line.

So capture the numbers before changing the format, not after.

### Reader pages — harness already exists, but it is compiled out

`EpubReaderActivity::runRenderBenchmark()` (`src/activities/reader/EpubReaderBenchmark.cpp`)
drives 10 page turns forward and 10 back and reports per-phase timings (prewarm, bwRender,
display, grey planes), `fontCacheHits`/`Misses`, `fontDecompressMs`, `fontGetBitmapTimeUs`,
`fontGetBitmapCalls` and heap min/max. That is exactly the right instrument — but it sits behind
`ENABLE_BENCHMARKS`, which defaults to 0 (`EpubReaderActivity.h:7-8`), so a stock `env:default`
build does not have it.

To capture: add `-DENABLE_BENCHMARKS=1` to `platformio.local.ini`, flash, open a book, then
Reader menu → Tools → Render Benchmark. Repeat per reader font family and size — those are what
move the reader numbers. `SETTINGS.uiFontSize` does not affect reader text at all.

### UI screens — nothing instrumented, because there is nothing to instrument

UI faces are uncompressed, so `GfxRenderer::getGlyphBitmap()` ends at
`&fontData->bitmap[glyph->dataOffset]` (`GfxRenderer.cpp:46`) — a direct flash read that never
reaches `FontDecompressor`. Every `FontDecompressor` counter is therefore **zero** on a menu
screen, and the only whole-screen signal is
`LOG_DBG("GFX", "Time = %lu ms from clearScreen to displayBuffer")` (`GfxRenderer.cpp:2909`),
which is the frame's entire compose cost, font work included but not separable.

That log line is still worth recording — it is the one that caught upstream's Home-screen
regression (14 ms → 70 ms) — but it cannot attribute a change to the font path.

### `env:bench_font` — the missing piece

`bench/font_main.cpp` measures the font layer directly on the C3, with no framebuffer and no
panel, so e-ink refresh cannot drown microsecond differences:

```
pio run -e bench_font -t upload
pio device monitor -e bench_font
```

**This replaces the firmware on the device.** The sketch has no UI and idles when it finishes, so
the device will look dead until you flash the reader back with `pio run -e default -t upload`.

It links the HAL (unavoidable — PlatformIO compiles every source in a discovered library
directory, and `lib/EpdFont` holds `SdCardFont.cpp`), but never initialises the display, so
nothing but the font path runs. Built size, for reference: 632,582 B flash, 24,684 B RAM.

| benchmark | what it isolates | why it is here |
| --- | --- | --- |
| `glyph_lookup` | interval search + returning a record pointer, ns/lookup | lever 1's **regression guard**, not its win — see "What the repack will and will not move" below |
| `text_measure` | lookup + kerning for a whole string, µs/call | the layout hot path; runs several times per visible row |
| `ui_bitmap_fetch` | reading an uncompressed glyph's bytes, µs/row | the number a compressed UI face would replace with a decode — decides whether that is ever affordable |
| `reader_cold` | one glyph with no prewarm: streams its group | the regime a compressed UI face would land in, since UI screens never prewarm |
| `reader_prewarm` / `reader_warm` | page prewarm (µs + heap) vs the per-glyph hit it buys | a codec change moves these two in opposite directions, so they must be read apart |

It covers both UI steps (`inter_ui_12` and `inter_ui_14`) so the larger UI font's own cost is on
the record, and `notosans_14` for the reader.

### Captured on device, X3, 2026-09-19

`env:bench_font`, before the glyph repack. This is the "before" column any format change is
judged against.

X3, 160 MHz, free heap 262,744 B, `sizeof(EpdGlyph) = 16`.

| benchmark | face | measured |
| --- | --- | ---: |
| `glyph_lookup` | inter_ui_12 (62 intervals) | **1082 ns** / lookup |
| `glyph_lookup` | inter_ui_14 (62 intervals) | **1080 ns** / lookup |
| `glyph_lookup` | notosans_14 (27 intervals) | **983 ns** / lookup |
| `kerning` | inter_ui_12 | **3052 ns** / pair |
| `kerning` | inter_ui_14 | **3052 ns** / pair |
| `kerning` | notosans_14 | **2189 ns** / pair |
| `text_measure` | inter_ui_12/row (18 ch) | **87 us** / call |
| `text_measure` | inter_ui_14/row (18 ch) | **87 us** / call |
| `text_measure` | inter_ui_12/set (39 ch) | **181 us** / call |
| `text_measure` | inter_ui_14/set (39 ch) | **181 us** / call |
| `ui_bitmap_fetch` | inter_ui_12/row | **40 us** / row (17 glyphs, 331 B) |
| `ui_bitmap_fetch` | inter_ui_14/row | **46 us** / row (17 glyphs, 438 B) |
| `text_measure` | notosans_14/page (392 ch) | **1992 us** / call |
| `reader_cold` | notosans_14 | **2796 us** / glyph; streamed 110,729 / 313,216 B (35%); peak ring 3827 B |
| `reader_prewarm` | notosans_14 | **11,692 us**; heap +2856 B; peak ring 2102 B; missed 0 |
| `reader_warm` | notosans_14 | **3691 ns** / glyph drawn; 100% hit rate |

### What the run says

**1. The larger UI font is free at runtime.** `text_measure` is *identical* between Inter 12 and
Inter 14 — 87/87 us for a menu row, 181/181 us for a settings row. Measurement never touches a
bitmap, so its cost is per-glyph lookup plus kerning and does not care how big the glyph is. Only
`ui_bitmap_fetch` grows, 40 -> 46 us per row (+15% for +32% of bitmap bytes, so sub-linear). At
~24 rows that is **+0.15 ms per screen**, against a frame that costs tens of ms. The 141 KB of
flash bought no measurable CPU.

**2. UI text measurement is the dominant UI font cost, and it is larger than expected.** 87 us
for 18 characters is **4.8 us/char** — roughly 4.5x a bare glyph lookup (1082 ns). The difference
is kerning, since measured at **3052 ns/pair**; see "Kerning is the dominant term" below. A list
screen measures every visible row.

**3. That interacts badly with `truncatedText()`, which is O(n^2).** When the text overflows it
re-measures the WHOLE remaining string after dropping each trailing character, and builds a fresh
`std::string` per iteration (`GfxRenderer.cpp`, the `while (... getTextWidth(fontId, (item +
ellipsis)...))` loop). Trimming ten characters off a 39-character title is therefore ~10 x 181 us
= **~1.8 ms for one row**. Rows that FIT pay one measurement and are fine, so this hits long book
titles and filenames in the browser, not short menu labels.

That is the biggest UI-side win visible in this data, it is pure CPU with no flash trade, and it
is independent of everything else in this document. The fix is to stop re-measuring from the
start: subtract the last glyph's advance plus kern as characters are dropped, or binary-search the
cut point. **Not done here** — it is a separate change.

**4. "UI faces must stay uncompressed" now has a number: 757x.** `reader_cold` is
**2796 us per glyph** against `reader_warm`'s 3691 ns. UI screens never prewarm, so a compressed
UI face would sit in the cold regime: ~30 distinct glyphs on a menu against a 4-slot fallback LRU
is tens of milliseconds per repaint. Upstream's Home-screen regression (14 ms -> 70 ms) is exactly
this, and it is why the Large step shipped a new Inter 14 instead of reusing a compressed Noto
Sans face. Revisit only after a glyph cache that outlives one screen.

**5. Ring streaming is doing its job.** 110,729 of 313,216 group bytes decoded (35%) and a peak
ring of 3827 B, where the old whole-group inflate would have needed a transient up to 64 KB.
Prewarm's own peak is lower still at 2102 B.

**6. `reader_prewarm` at 11.7 ms per page is worth watching.** It buys the 757x, so it is a good
trade, but it is a fixed per-page cost that any per-glyph codec would grow. Read it together with
`reader_warm`, never alone.

### Kerning is the dominant term, and it is a cache problem

Measured, having previously been inferred from table sizes — and the inference was too low. Per
character of UI text measurement:

| term | inter_ui_12 | share |
| --- | ---: | ---: |
| `text_measure` (87 us / 18 chars) | 4.83 us | 100% |
| kerning | 3.05 us | **63%** |
| glyph lookup | 1.08 us | 22% |
| everything else (UTF-8 decode, ligature and combining checks, fp4, min/max, loop) | ~0.70 us | 15% |

The subtraction is indicative rather than exact — the kerning benchmark walks pairs from the
50-codepoint sweep while `text_measure` walks an English sentence, so the two touch different
parts of the table. The caveat-free statement is the direct ratio: **one kern lookup costs about
2.8x one glyph lookup.**

**Why it is so expensive is not the probe count.** Compare the two faces against the C3's 16 KB
cache:

| face | kern data | class entries | probes/pair | measured | ns/probe |
| --- | ---: | ---: | ---: | ---: | ---: |
| interval table (for reference) | 744 B | 62 intervals | ~6 | — | ~83 |
| notosans_14 | 4,355 B | 481 / 476 | ~17.8 | 2189 ns | ~123 |
| inter_ui_12 | **10,003 B** | 744 / 743 | ~19 | 3052 ns | ~161 |

The probe counts differ by 7%; the times differ by 39%. What tracks the cost is the **working set**
— 744 B, then 4.4 KB, then 10 KB — and the per-probe cost rises monotonically with it. That is
consistent with the kern lookups missing the flash cache where the interval table stays resident.
Three points is not proof, but it is the only variable that moves with the measurement, and it
points the fix at footprint rather than at algorithmic probe count.

Note this hits the reader too: at 2189 ns/pair, one 392-character page costs ~858 us of kerning
per measurement pass, and a render makes several.

**Options, unranked and uncosted on device:**

1. **Memoize the class lookups per codepoint.** `getKerning()` spends its time in two binary
   searches that map a codepoint to a class id — a pure function of (font, codepoint). A small
   direct-mapped cache would turn ~19 probes into an array index for any character already seen.
   UI text repeats heavily (an alphabet's worth of codepoints across a whole screen), so the hit
   rate after the first row would be very high. Costs RAM in `.bss`, which is a permanent ceiling
   reduction — see [[permanent-block-placement-beats-size]] — so it wants to be small, 512 B
   rather than 4 KB. Helps the reader as much as the UI. No format change, no visual change.
2. **Restructure the tables to stay resident** (perfect hash, two-level index). Generator work,
   unknown flash delta.
3. **Skip kerning for UI text.** Instantly returns the 63%, but `drawText()` kerns too, so both
   paths would have to change together, and it is a visible rendering change.

Option 1 is pure memoization of a pure function and is the obvious first probe. None of this is
a flash lever — it buys CPU, not bytes.

### What the repack will and will not move

**Correction to an earlier reading of this document.** `glyph_lookup` was described as the number
the glyph repack moves downward. The measured spread shows that is wrong, and the code confirms
why: `EpdFont::findGlyph()` (`EpdFont.cpp:218-245`) ends at
`return &data->glyph[interval.offset + (cp - interval.first)]` — a pointer computation. **Lookup
never reads the record**, so the record's width barely enters this path.

What dominates instead is the `std::upper_bound` over the interval table, and the interval count
predicts the spread exactly: 62 intervals (Inter) → 1082 ns, 27 intervals (Noto Sans 14) → 982 ns.
That gap is ~1.2 extra binary-search probes, i.e. **~83 ns per probe** — about 13 cycles at
160 MHz for a flash-resident 12-byte read plus a compare. Consistent, and it means roughly half of
each lookup is interval search.

Consequences for the repack PR:

1. **Expect `glyph_lookup` to be flat, not faster.** The 50-codepoint sweep touches 800 B of
   records, cache-resident at either width, so halving the array cannot show up here. The array
   only *just* exceeds the C3's 16 KB cache today (1031 × 16 = 16,496 B for Inter; 8,248 B
   repacked), so a locality win exists in principle but needs a workload touching ~1000 distinct
   glyphs, which no screen does.
2. **The real risk is the opposite direction.** The recommended
   `bool getGlyph(uint32_t cp, EpdGlyph& out)` signature *adds* per-lookup work: unpacking 8 bytes
   into the 16-byte working struct and deriving `dataLength` from `w * h * bpp`. At 4.8 us/char for
   measurement, a few hundred extra ns per glyph is a few percent on every menu row. So
   `glyph_lookup` and `text_measure` are the PR's **regression guards**: they must not go up.
3. **There is a way to make them go down instead.** Measurement needs only `advanceX` — width,
   height, left, top, dataOffset and dataLength are drawing-only. A repack that also offers a
   2-byte `advanceOf(cp)` path, skipping the unpack entirely, would make `text_measure` *faster*
   than today while still freeing the 384 KB. Worth designing in from the start rather than
   retrofitting.

So lever 1 remains a **flash** lever that is CPU-neutral, not a CPU win. That is still the reason
to do it first: it is the only lever on the list that does not have to buy its bytes with cycles.

**Side observation, cheap and unrelated:** the UI faces carry 62 intervals against Noto Sans 14's
27, costing them ~100 ns per lookup (~10%). Those come from the 13 `--additional-intervals` ranges
in `convert-builtin-fonts.sh`, split further by coverage gaps. Merging adjacent ranges would shave
probes off every UI glyph for free. The interval tables are only 26,916 B, so this is a CPU tidy,
not a flash one.

### Whole-screen context — measured, and it reframes everything above

Captured on an X3 from the ordinary `env:default` firmware (NOT `bench_font`, which has no
display), browsing Home and then Settings. Both traces are the same build with the UI font size
feature in; they differ only in which ladder step is selected, and neither has the kern-class
cache.

| screen | UI font | compose (`clearScreen`->`displayBuffer`) | panel | frame | compose share |
| --- | --- | ---: | ---: | ---: | ---: |
| Home, moving the selection | Normal | 21-23 ms | 434-436 ms FAST | ~457 ms | **4.7%** |
| Home, moving the selection | **Large** | 22-24 ms | 435-436 ms FAST | ~459 ms | **5.0%** |
| Settings, moving the selection | Normal | 36-40 ms | 435-437 ms FAST | ~474 ms | **7.9%** |
| Settings, moving the selection | **Large** | 37-40 ms | 435-436 ms FAST | ~475 ms | **8.1%** |
| Settings, on entry | either | 37-38 ms | **2185-2187 ms HALF** | ~2224 ms | 1.7% |
| Home, first paint after boot | Large | 66 ms (cold: cover check + stores) | 436 ms | — | — |

**The panel is 92-95% of every screen update.** Compose — where all font work lives: measurement,
kerning, glyph lookup, blitting and truncation together — is 22-24 ms on Home and 37-40 ms on
Settings.

**The larger UI font costs about 1 ms of compose and no measurable RAM.** Home goes 21-23 -> 22-24
and Settings 36-40 -> 37-40, i.e. within noise of each other. That is the microbenchmark's
prediction landing: `text_measure` was identical between Inter 12 and Inter 14 (measurement never
touches a bitmap), and only `ui_bitmap_fetch` grew, 40 -> 47 us per row, for ~+0.15 ms per screen.
Steady-state heap is the same on both steps (`Free: ~39.9 KB`, `contig 23,540 B`), which is what
you would expect from font data that lives in flash.

It also validates the ladder's self-check: `applyUiFontScale()` LOG_ERRs if `UiFontLadder::STEPS`
disagrees with the advanceY of the fonts it just bound, and that line does not appear in either
trace at either step.

The frame budget changes the conclusions about everything else in this document:

- **Nothing in the font path can be perceptible here.** Eliminating *all* compose work on Settings
  would take a repaint from ~475 ms to ~436 ms. The e-ink waveform sets the floor.
- **Settings composes 1.7x Home** (38 vs 23 ms), consistent with it being the text-heavy screen —
  a dozen rows each measured, truncated and drawn, against Home's cover plus a few rows. So ~15 ms
  is roughly the font path's whole budget on a list screen, and the optimisations here target a
  few ms of it.
- **The kern-class cache costs 512 B of `.bss` for ~2-3 ms of a 475 ms frame.** Against the heap
  this device actually runs at — `Free: 38,092 B, contig 23,540 B, Min Free: 18,424 B` in the same
  trace — that is a poor trade, and `.bss` is a permanent reduction of the ceiling rather than
  merely occupied heap (docs/memory-allocation-strategy.md). Recommend NOT shipping it on this
  hardware.
- **The truncation rewrite costs no RAM at all** (+866 B of flash) and *removes* per-row heap
  allocations, so it lowers churn rather than raising it. Keep it on those grounds, not for the
  wall-clock.
- **What this validates is the structural decision, not the micro-optimisations.** A compressed UI
  face at 2796 us/glyph cold would have added ~84 ms of compose to a menu screen — over 3x the
  whole Home compose budget and ~18% of the frame. That one WOULD have been visible, and the
  margin is exactly what made shipping Inter 14 the right call rather than reusing a compressed
  Noto Sans face.

One number worth a separate look, unrelated to fonts: entering an activity costs a **HALF refresh
at ~2186 ms**, five times a FAST one, and it is reproducible across both traces. One event per
activity entry, and it dwarfs everything this document measures.

Caveat: the traces show the CPU dropping to low-power mode between repaints
(`[PWR] Going to low-power mode` / `Restoring normal CPU frequency`, and one `CPU: 10 MHz`
sample). Compose at 22 ms is a 160 MHz figure; at 10 MHz it would be ~350 ms and the balance above
would invert. The restore lines say that is not happening on a repaint today, but it is the one
condition under which font CPU would start to matter.

## Ranked recommendation

| # | lever | flash | CPU | RAM | compat |
| --- | --- | ---: | --- | --- | --- |
| 1 | built-in glyph record 16 → 8 B | **−384,504 B** | none (slightly better) | none | internal only |
| 2 | `.cpfont` record 16 → 10 B | −5.7% per file | none | none | format v5, regenerate fonts |
| 3 | global packed-glyph cache | 0 | **faster** | +8–10 KB | none |
| 4 | brotli bitmaps | −95 KB gross, −55/−70 net | modest | none | internal only |
| 5 | GlyphStream codec | ~−650 KB | +107% unless 3 lands first | +8–10 KB | needs 3, and upstream is a draft |

Lever 1 alone is 2.8× the 5% target and pays back the 141 KB the larger UI font cost, with 243 KB
left over.
