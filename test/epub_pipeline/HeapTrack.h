#pragma once
// Whole-process heap tracking for the epub_pipeline_dump benchmark mode.
// Linked ONLY into the CLI tool — it overrides malloc/free process-wide
// (same header-word technique as test/epub_benchmark), which is unwanted
// inside the gtest binary.
#include <cstddef>

// Zero the live/peak counters and start tracking.
void heapTrackBegin();
// Stop tracking and return the peak live bytes observed since begin().
size_t heapTrackEnd();
// Suspend/resume counting without clearing the counters. The dump harness reads every page back
// through the same Section code the build uses, and that read-back is test scaffolding that never
// runs on device -- profiling it made loadPageFromSectionFile the largest "allocation site" in the
// whole book. Bracket anything that is not the build.
void heapTrackPause();
void heapTrackResume();
// Number of allocations since begin(). Peak bytes alone hides allocation CHURN — many
// short-lived blocks can leave peak flat while still fragmenting a no-compaction heap,
// which is the failure mode on the ESP32-C3. Valid after heapTrackEnd().
size_t heapTrackAllocCount();
// Power-of-two size histogram of allocations since begin(): [0]=<=16B, [1]=<=32B, ... [11]=>16KB.
// Fills up to `count` buckets. Reveals which allocations dominate by COUNT, which is what
// fragments a no-compaction heap.
void heapTrackSizeHistogram(size_t* out, int count);
// Top allocation SITES, as raw return addresses (symbolize with addr2line). Writes up to `count`
// entries into out[]; returns how many were written, ordered by peakLive.
//
// `bytes` is CUMULATIVE and answers "who allocates a lot" -- on a churning parser that is almost
// always the per-word string traffic, and almost always the wrong thing to change. `peakLive` is
// what the site was still HOLDING when the run hit its high-water mark, which is what a 380 KB
// device actually runs out of.
struct HeapTrackSite {
  unsigned long long pc;
  size_t count;
  size_t bytes;
  size_t peakLive;
  // Largest single allocation this site ever made. Names the sites behind the >=8 KB buckets,
  // which cumulative bytes cannot: a 33 KB inflate ring made once per spine is invisible next
  // to per-word string traffic in either `bytes` or `count`.
  size_t maxSize;
};
int heapTrackTopSites(HeapTrackSite* out, int count);
// Peak live bytes so far, readable while tracking is active (heapTrackEnd() stops tracking).
// Lets a per-spine hook see which spine moved the high-water mark.
size_t heapTrackPeakSoFar();

// --- Per-build window (epub_build_inventory) -----------------------------------------------
// The whole-run peak above answers "what did the book cost"; a background build is judged on its
// OWN peak above the heap it started from, and on who holds what at that instant. A window
// records every site's live bytes at its start, snapshots them again at the window's peak (to
// within kWindowSnapStep bytes) and at its end, and counts the allocations made inside it.
void heapTrackWindowBegin();
void heapTrackWindowEnd();
size_t heapTrackLive();
struct HeapTrackWindowSummary {
  size_t base = 0;    // live bytes when the window opened
  size_t peak = 0;    // highest live bytes inside it
  size_t atSnap = 0;  // live bytes when the peak snapshot was taken (trails peak by < step)
  size_t end = 0;     // live bytes when it closed
};
HeapTrackWindowSummary heapTrackWindowSummary();
// Peak since the previous mark (or the window start), then restart the phase peak at the current
// live figure. Call at each build-phase boundary.
size_t heapTrackPhaseMark();
// Invoked from INSIDE the malloc hook whenever the window-peak snapshot is retaken, so it must
// not allocate. The inventory tool uses it to capture the arena's contents at the heap's peak.
void heapTrackSetWindowPeakCallback(void (*callback)());
struct HeapTrackWindowSite {
  unsigned long long pc = 0;
  unsigned long long ctx1 = 0;  // the next two frames up, for context (first allocation seen)
  unsigned long long ctx2 = 0;
  size_t winCount = 0;   // allocations made inside the window
  size_t winBytes = 0;   // their cumulative bytes
  size_t startLive = 0;  // live at window start
  size_t peakLive = 0;   // live at the window-peak snapshot
  size_t endLive = 0;    // live at window end
  size_t maxSize = 0;    // largest single allocation ever made by the site
};
// Every site the window touched or that held memory at its start/peak/end.
int heapTrackWindowSites(HeapTrackWindowSite* out, int count);
