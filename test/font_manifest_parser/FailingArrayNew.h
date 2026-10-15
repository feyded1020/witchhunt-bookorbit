#pragma once

#include <cstddef>

// From now on, every nothrow array new of at least `bytes` returns null; 0 turns that off. The code
// under test allocates its blocks with makeUniqueNoThrow<uint8_t[]>, which is such a new; nothing else
// in these tests is, so the hook fails exactly the allocations a device would fail on a tight heap.
void failNothrowArrayNewFrom(size_t bytes);

// failNothrowArrayNewFrom() for one scope.
class FailingArrayNew {
 public:
  explicit FailingArrayNew(const size_t bytes) { failNothrowArrayNewFrom(bytes); }
  ~FailingArrayNew() { failNothrowArrayNewFrom(0); }
  FailingArrayNew(const FailingArrayNew&) = delete;
  FailingArrayNew& operator=(const FailingArrayNew&) = delete;
};
