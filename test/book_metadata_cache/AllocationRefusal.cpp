#include "AllocationRefusal.h"

#include <cstdlib>
#include <new>

namespace {
// 0: nothing refused.
size_t refuseFrom = 0;

bool refused(const size_t bytes) { return refuseFrom != 0 && bytes >= refuseFrom; }

void* allocateOrNull(const size_t bytes) {
  if (refused(bytes)) return nullptr;
  return std::malloc(bytes == 0 ? 1 : bytes);
}

void* allocateOrThrow(const size_t bytes) {
  void* p = allocateOrNull(bytes);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
}  // namespace

AllocationRefusal::AllocationRefusal(const size_t bytes) { refuseFrom = bytes; }
AllocationRefusal::~AllocationRefusal() { refuseFrom = 0; }

// The library's default operator delete releases with std::free, so malloc-backed replacements of
// the allocating forms stay compatible with it.
void* operator new(const size_t bytes) { return allocateOrThrow(bytes); }
void* operator new[](const size_t bytes) { return allocateOrThrow(bytes); }
void* operator new(const size_t bytes, const std::nothrow_t&) noexcept { return allocateOrNull(bytes); }
void* operator new[](const size_t bytes, const std::nothrow_t&) noexcept { return allocateOrNull(bytes); }
