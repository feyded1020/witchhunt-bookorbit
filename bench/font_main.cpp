// ESP32-C3 font-path benchmark
//
// Measures the per-glyph cost of the font layer on real hardware, split the way the flash levers
// split it (see docs/font-flash-analysis-2026-09-19.md):
//
//   * UI faces are UNCOMPRESSED 1-bit. `getGlyphBitmap()` is a direct flash read, so their cost
//     is glyph lookup + kerning + reading the bitmap through the instruction cache. Nothing in
//     the firmware instruments this today — UI screens never prewarm, so the FontDecompressor
//     counters are all zero for them and the only signal is whole-frame compose time.
//   * Reader faces are 2-bit per-group DEFLATE. Their cost has two regimes that differ by three
//     orders of magnitude: a prewarmed page-slot hit, and a fallback that streams the group.
//
// Why this exists: every remaining font-flash lever (narrower glyph records, brotli, a per-glyph
// codec) trades bytes for glyph-path CPU, and crosspoint-reader#3083 stalled precisely because
// there was no way to measure that trade. `glyph_lookup` is the number a narrower glyph record
// moves; `ui_bitmap_fetch` is the number that decides whether a UI face may ever be compressed.
//
// Deliberately does NOT go through GfxRenderer: a framebuffer and a panel would put e-ink
// refresh (hundreds of ms) on top of microsecond measurements and drown them.
//
// Build & flash:  pio run -e bench_font -t upload
// Monitor:        pio device monitor -e bench_font

#include <Arduino.h>
#include <EpdFont.h>
#include <FontDecompressor.h>
#include <builtinFonts/inter_ui_12_regular.h>
#include <builtinFonts/inter_ui_14_regular.h>
#include <builtinFonts/notosans_14_regular.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

// ---------------------------------------------------------------------------
// Timing / heap
// ---------------------------------------------------------------------------

static int64_t t0;
static void timerStart() { t0 = esp_timer_get_time(); }
static int64_t timerElapsedUs() { return esp_timer_get_time() - t0; }
static size_t snapHeap() { return esp_get_free_heap_size(); }

// Every benchmark folds its result into this. Reading it at the end is what stops the compiler
// deleting the work: a glyph lookup whose result is unused is a dead store, and -O2 will remove
// the entire loop. `volatile` on the accumulator would instead add a store per iteration and
// measure that; folding into a plain global and printing it once does not.
static uint32_t sink = 0;

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// A menu row: what a list screen actually measures and draws, once per visible row.
static const char* kMenuRow = "Reading statistics";
// A settings row with its value, i.e. the longest single line the UI lays out.
static const char* kSettingsRow = "Inline footnote previews        Enabled";
// A page of body text. Length matters more than content: prewarm cost scales with the number of
// DISTINCT glyphs, and a page of English prose saturates that at ~70 within a sentence or two.
static const char* kReaderPage =
    "The sun had not yet risen. The sea was indistinguishable from the sky, except that the sea "
    "was slightly creased as if a cloth had wrinkles in it. Gradually as the sky whitened a dark "
    "line lay on the horizon dividing the sea from the sky and the grey cloth became barred with "
    "thick strokes moving, one after another, beneath the surface, following each other, pursuing "
    "each other, perpetually.";

// The codepoints a glyph-lookup sweep should cover, weighted the way real text is: mostly ASCII,
// a few accented Latin, and one Cyrillic run. Interval lookup is a binary search over the
// font's interval table, so WHERE a codepoint sits changes its cost — an ASCII-only sweep would
// flatter the measurement.
// clang-format off
static const uint32_t kSweep[] = {
    'e', 't', 'a', 'o', 'i', 'n', 's', 'h', 'r', 'd', 'l', 'u', 'c', 'm', 'f', 'w', 'y', 'p', 'g',
    'b', 'v', 'k', 'x', 'j', 'q', 'z', 'E', 'T', 'A', 'O', 'I', 'N', ' ', '.', ',', '-',
    0x0027 /* ' */, 0x0022 /* " */,
    0x00E9 /* é */, 0x00FC /* ü */, 0x00F1 /* ñ */, 0x00E5 /* å */, 0x0107 /* ć */, 0x0142 /* ł */,
    0x0410 /* А */, 0x0431 /* б */, 0x0432 /* в */, 0x0435 /* е */, 0x043E /* о */, 0x044F /* я */,
};
// clang-format on
static constexpr int kSweepCount = sizeof(kSweep) / sizeof(kSweep[0]);

static FontDecompressor decompressor;

// ---------------------------------------------------------------------------
// 1. Glyph lookup — interval binary search + glyph record read.
//
// This is the unit a narrower glyph record changes: 8 bytes instead of 16 halves what the
// instruction cache has to pull in per lookup, so this number should go DOWN, not up.
// ---------------------------------------------------------------------------

static void benchGlyphLookup(const char* label, const EpdFontData* data) {
  const EpdFont font(data);
  constexpr int REPS = 200;

  // Warm the flash cache so the first pass does not pay for everyone.
  for (int i = 0; i < kSweepCount; ++i) sink += (uint32_t)(uintptr_t)font.getGlyph(kSweep[i]);

  timerStart();
  for (int r = 0; r < REPS; ++r) {
    for (int i = 0; i < kSweepCount; ++i) {
      const EpdGlyph* g = font.getGlyph(kSweep[i]);
      if (g) sink += g->width + g->height + g->advanceX;
    }
  }
  const int64_t us = timerElapsedUs();
  const int32_t ops = REPS * kSweepCount;

  Serial.printf("BENCH glyph_lookup      %-18s total=%6lldus  per_lookup=%4lldns  ops=%ld\n", label, us,
                (us * 1000) / ops, (long)ops);
}

// ---------------------------------------------------------------------------
// 2. Text measurement — lookup + kerning for a whole string.
//
// The layout hot path. Every row is measured before it is drawn, and truncatedText() re-measures
// prefix by prefix, so this runs several times per visible row.
// ---------------------------------------------------------------------------

static void benchTextMeasure(const char* label, const EpdFontData* data, const char* text) {
  const EpdFont font(data);
  constexpr int REPS = 200;
  int w = 0, h = 0;

  font.getTextDimensions(text, &w, &h);  // warm
  timerStart();
  for (int r = 0; r < REPS; ++r) {
    font.getTextDimensions(text, &w, &h);
    sink += (uint32_t)w;
  }
  const int64_t us = timerElapsedUs();

  Serial.printf("BENCH text_measure      %-18s total=%6lldus  per_call=%5lldus  w=%dpx  chars=%u\n", label, us,
                us / REPS, w, (unsigned)strlen(text));
}

// ---------------------------------------------------------------------------
// 2b. Kerning alone.
//
// Isolated because arithmetic over the table sizes said it, not the glyph lookup, is the biggest
// single term in text measurement -- ~9.5 binary-search probes for the left class plus ~9.5 for
// the right against 744-entry tables, against ~6 for the interval search. That was inference from
// symbol sizes; this measures it. If it holds, kerning is where a measurement optimisation should
// go, and the glyph record is not.
//
// Walks adjacent pairs of the sweep so the class lookups hit a realistic spread of codepoints
// rather than one cached row.
// ---------------------------------------------------------------------------

static void benchKerning(const char* label, const EpdFontData* data) {
  const EpdFont font(data);
  constexpr int REPS = 200;

  for (int i = 1; i < kSweepCount; ++i) sink += (uint32_t)font.getKerning(kSweep[i - 1], kSweep[i]);  // warm

  timerStart();
  for (int r = 0; r < REPS; ++r) {
    for (int i = 1; i < kSweepCount; ++i) {
      sink += (uint32_t)(int32_t)font.getKerning(kSweep[i - 1], kSweep[i]);
    }
  }
  const int64_t us = timerElapsedUs();
  const int32_t ops = REPS * (kSweepCount - 1);

  Serial.printf("BENCH kerning           %-18s total=%6lldus  per_pair=%4lldns  ops=%ld\n", label, us,
                (us * 1000) / ops, (long)ops);
}

// ---------------------------------------------------------------------------
// 3. UI bitmap fetch — the uncompressed path, start to finish.
//
// Sums the glyph's actual bitmap bytes rather than just taking its address: the address is
// arithmetic, and the cost being measured is pulling those bytes through the flash cache. That
// read is what a compressed UI face would replace with a decode, which is the whole question.
// ---------------------------------------------------------------------------

static void benchUiBitmapFetch(const char* label, const EpdFontData* data, const char* text) {
  const EpdFont font(data);
  constexpr int REPS = 200;

  if (data->groups != nullptr) {
    Serial.printf("BENCH ui_bitmap_fetch   %-18s SKIPPED (font is compressed)\n", label);
    return;
  }

  int glyphs = 0;
  uint32_t bytes = 0;
  timerStart();
  for (int r = 0; r < REPS; ++r) {
    for (const char* p = text; *p; ++p) {  // ASCII fixtures, so byte == codepoint
      const EpdGlyph* g = font.getGlyph((uint32_t)(uint8_t)*p);
      if (!g || g->dataLength == 0) continue;
      const uint8_t* bm = &data->bitmap[g->dataOffset];
      for (uint16_t b = 0; b < g->dataLength; ++b) sink += bm[b];
      if (r == 0) {
        glyphs++;
        bytes += g->dataLength;
      }
    }
  }
  const int64_t us = timerElapsedUs();

  Serial.printf("BENCH ui_bitmap_fetch   %-18s total=%6lldus  per_row=%5lldus  glyphs=%d  bitmap=%luB\n", label, us,
                us / REPS, glyphs, (unsigned long)bytes);
}

// ---------------------------------------------------------------------------
// 4. Reader bitmap fetch, COLD — no prewarm, so every glyph streams its group.
//
// The regime a UI screen would land in if its face were compressed, because UI screens never
// prewarm. Kept to one pass: it is thousands of microseconds per glyph and the point is the
// order of magnitude, not a tight average.
// ---------------------------------------------------------------------------

static void benchReaderBitmapCold(const char* label, const EpdFontData* data) {
  if (data->groups == nullptr) {
    Serial.printf("BENCH reader_cold       %-18s SKIPPED (font is uncompressed)\n", label);
    return;
  }
  const EpdFont font(data);
  decompressor.clearCache();
  decompressor.resetStats();

  int glyphs = 0;
  timerStart();
  for (int i = 0; i < kSweepCount; ++i) {
    const EpdGlyph* g = font.getGlyph(kSweep[i]);
    if (!g) continue;
    const uint32_t idx = (uint32_t)(g - data->glyph);
    const uint8_t* bm = decompressor.getBitmap(data, g, idx);
    if (bm) {
      sink += bm[0];
      glyphs++;
    }
  }
  const int64_t us = timerElapsedUs();
  const auto& st = decompressor.getStats();

  Serial.printf(
      "BENCH reader_cold       %-18s total=%6lldus  per_glyph=%5lldus  glyphs=%d  streamed=%luB/%luB  peak_ring=%luB\n",
      label, us, glyphs ? us / glyphs : 0, glyphs, (unsigned long)st.streamedBytes, (unsigned long)st.groupBytes,
      (unsigned long)st.peakTempBytes);
}

// ---------------------------------------------------------------------------
// 5. Reader prewarm + WARM fetch — the path a real page turn takes.
//
// Reports the prewarm itself (once per page) separately from the per-glyph hit it buys (once per
// glyph drawn), because a codec change moves those two in opposite directions.
// ---------------------------------------------------------------------------

static void benchReaderPrewarm(const char* label, const EpdFontData* data) {
  if (data->groups == nullptr) {
    Serial.printf("BENCH reader_prewarm    %-18s SKIPPED (font is uncompressed)\n", label);
    return;
  }
  const EpdFont font(data);
  decompressor.clearCache();
  decompressor.resetStats();

  const size_t heapBefore = snapHeap();
  timerStart();
  const int missed = decompressor.prewarmCache(data, kReaderPage);
  const int64_t prewarmUs = timerElapsedUs();
  const size_t heapAfter = snapHeap();
  const auto& pw = decompressor.getStats();

  Serial.printf("BENCH reader_prewarm    %-18s prewarm=%6lldus  heap=%+dB  peak_ring=%luB  missed=%d\n", label,
                prewarmUs, (int)heapBefore - (int)heapAfter, (unsigned long)pw.peakTempBytes, missed);

  // Warm hits. 20 reps because a single pass over one page is only ~70 lookups and the per-glyph
  // cost here is around a microsecond.
  constexpr int REPS = 20;
  decompressor.resetStats();
  int glyphs = 0;
  timerStart();
  for (int r = 0; r < REPS; ++r) {
    for (const char* p = kReaderPage; *p; ++p) {
      const EpdGlyph* g = font.getGlyph((uint32_t)(uint8_t)*p);
      if (!g) continue;
      const uint32_t idx = (uint32_t)(g - data->glyph);
      const uint8_t* bm = decompressor.getBitmap(data, g, idx);
      if (bm) {
        sink += bm[0];
        if (r == 0) glyphs++;
      }
    }
  }
  const int64_t us = timerElapsedUs();
  const auto& st = decompressor.getStats();
  const uint32_t total = st.cacheHits + st.cacheMisses;

  Serial.printf("BENCH reader_warm       %-18s total=%6lldus  per_glyph=%4lldns  draws=%d  hit_rate=%.1f%%\n", label,
                us, glyphs ? (us * 1000) / (glyphs * REPS) : 0, glyphs, total ? 100.0f * st.cacheHits / total : 0.0f);

  decompressor.clearCache();
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n=== Font path ESP32-C3 benchmark ===");
  Serial.printf("CPU: %u MHz   free heap: %u B\n", (unsigned)getCpuFrequencyMhz(), (unsigned)esp_get_free_heap_size());
  Serial.printf("sizeof(EpdGlyph) = %u B   (the glyph record; see the repack lever in the docs)\n\n",
                (unsigned)sizeof(EpdGlyph));

  if (!decompressor.init()) {
    Serial.println("FATAL: FontDecompressor::init() failed");
    return;
  }

  Serial.println("-- UI faces (uncompressed 1-bit; UI screens never prewarm) --");
  benchGlyphLookup("inter_ui_12", &inter_ui_12_regular);
  benchGlyphLookup("inter_ui_14", &inter_ui_14_regular);
  benchKerning("inter_ui_12", &inter_ui_12_regular);
  benchKerning("inter_ui_14", &inter_ui_14_regular);
  benchTextMeasure("inter_ui_12/row", &inter_ui_12_regular, kMenuRow);
  benchTextMeasure("inter_ui_14/row", &inter_ui_14_regular, kMenuRow);
  benchTextMeasure("inter_ui_12/set", &inter_ui_12_regular, kSettingsRow);
  benchTextMeasure("inter_ui_14/set", &inter_ui_14_regular, kSettingsRow);
  benchUiBitmapFetch("inter_ui_12/row", &inter_ui_12_regular, kMenuRow);
  benchUiBitmapFetch("inter_ui_14/row", &inter_ui_14_regular, kMenuRow);

  Serial.println("\n-- Reader face (2-bit, per-group DEFLATE) --");
  benchGlyphLookup("notosans_14", &notosans_14_regular);
  benchKerning("notosans_14", &notosans_14_regular);
  benchTextMeasure("notosans_14/page", &notosans_14_regular, kReaderPage);
  benchReaderBitmapCold("notosans_14", &notosans_14_regular);
  benchReaderPrewarm("notosans_14", &notosans_14_regular);

  Serial.printf("\nfree heap after: %u B   minimum ever: %u B\n", (unsigned)esp_get_free_heap_size(),
                (unsigned)esp_get_minimum_free_heap_size());
  Serial.printf("(sink=%lu — printed only so the optimiser keeps the work above)\n", (unsigned long)sink);
  Serial.println("=== done ===");
  Serial.println();
  Serial.println("This sketch is the font benchmark ONLY -- no display, no UI, no reader. The");
  Serial.println("device now sits idle and unresponsive; that is the end of the run, not a hang.");
  Serial.println("Reflash the firmware to get the reader back:");
  Serial.println("    pio run -e default -t upload");
  Serial.println("Whole-screen frame times ([GFX] Time = N ms from clearScreen to displayBuffer)");
  Serial.println("come from THAT build, not this one.");
}

void loop() {
  // Nothing left to do, but keep saying so: the first run of this sketch read as a crash purely
  // because the port went silent after the results.
  delay(30000);
  Serial.println("[idle] benchmark finished; reflash the reader with: pio run -e default -t upload");
}
