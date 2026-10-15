#pragma once

#include <esp_heap_caps.h>

#include <cstddef>

// Whether the heap holds one free block of at least `bytes`.
//
// For the growth of std containers (vector::resize/reserve, string::reserve), which cannot fail
// gracefully on the device: it is built with -fno-exceptions, so a failed operator new aborts and
// reboots it. Where the size comes from data -- a count read from a cache file -- and the caller
// has something sensible to do without the allocation, ask first. Found the hard way on the X3
// (2026-10-01): a 4 KB string reserve on a heap with 14.7 KB free but no block over 4.3 KB.
//
// The answer can go stale if another task allocates in between; the margin covers the allocator's
// own block header and a little of that, not a concurrent burst.
inline bool heapHasBlockFor(size_t bytes) {
  constexpr size_t ALLOCATOR_SLACK = 64;
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT) >= bytes + ALLOCATOR_SLACK;
}
