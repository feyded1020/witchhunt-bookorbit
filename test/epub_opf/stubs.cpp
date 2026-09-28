#include <cstddef>
#include <new>
#include <string>
#include <vector>

#include "../../lib/Epub/Epub/BookMetadataCache.h"

namespace opf_test_hooks {
std::vector<std::string>* g_spineHrefSink = nullptr;
// Nonzero: nothrow array new refuses any request larger than this many bytes and counts the
// refusal, so a test can make the heap turn down ContentOpfParser's index growth.
size_t g_refuseNothrowArraysAbove = 0;
size_t g_refusedNothrowArrays = 0;
}  // namespace opf_test_hooks

void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (opf_test_hooks::g_spineHrefSink != nullptr) {
    opf_test_hooks::g_spineHrefSink->push_back(href);
  }
}

// Replaces the global nothrow array new for this test binary: makeUniqueNoThrow<T[]>, which grows
// the manifest index, lands here. Unarmed it defers to the throwing operator new[], exactly as the
// default nothrow form does, so the default operator delete[] still pairs with it.
void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
  if (opf_test_hooks::g_refuseNothrowArraysAbove != 0 && size > opf_test_hooks::g_refuseNothrowArraysAbove) {
    ++opf_test_hooks::g_refusedNothrowArrays;
    return nullptr;
  }
  try {
    return ::operator new[](size);
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}
