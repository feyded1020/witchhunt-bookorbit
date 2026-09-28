// No-op HeapTrack for the dump variant that must NOT override malloc/free
// process-wide (the override deadlocks under the MinGW runtime).
#include "HeapTrack.h"

void heapTrackBegin() {}
size_t heapTrackEnd() { return 0; }
size_t heapTrackAllocCount() { return 0; }
void heapTrackSizeHistogram(size_t* out, const int count) {
  for (int i = 0; i < count; i++) out[i] = 0;
}
void heapTrackPause() {}
void heapTrackResume() {}
int heapTrackTopSites(HeapTrackSite*, int) { return 0; }
size_t heapTrackPeakSoFar() { return 0; }
void heapTrackWindowBegin() {}
void heapTrackWindowEnd() {}
size_t heapTrackLive() { return 0; }
HeapTrackWindowSummary heapTrackWindowSummary() { return {}; }
size_t heapTrackPhaseMark() { return 0; }
void heapTrackSetWindowPeakCallback(void (*)()) {}
int heapTrackWindowSites(HeapTrackWindowSite*, int) { return 0; }
