#include "FailingArrayNew.h"

#include <cstdlib>
#include <new>

namespace {
size_t failFrom = 0;
}  // namespace

void failNothrowArrayNewFrom(const size_t bytes) { failFrom = bytes; }

// Replaces the global nothrow array new for this test program. The library's delete[] frees with
// free(), so a block from malloc here is released correctly.
void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
  if (failFrom != 0 && size >= failFrom) return nullptr;
  return std::malloc(size == 0 ? 1 : size);
}
