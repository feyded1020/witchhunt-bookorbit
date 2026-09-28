#pragma once

#include <algorithm>
#include <cstdint>

// Issue #325: a chapter the TOC lists once can be split over several spine items (J-Novel Club
// books cut the text around every illustration), and the status bar's page counter, which counts
// one spine item, restarted at 1 in each piece. ChapterPageSpan holds what the reader knows about
// the OTHER files of the current chapter and turns the current file's page/total into the
// chapter's.
//
// A sibling whose page count has been recorded (SpinePageIndex) contributes it exactly. The rest
// are estimated from their byte size at the pages-per-byte of everything counted exactly (the
// current file included), at least one page each, and make the result approximate. A
// default-constructed span (a chapter of one file) passes the file's numbers through unchanged.
struct ChapterPageSpan {
  uint32_t pagesBefore = 0;    // exact pages of the counted siblings before the current file
  uint32_t pagesAfter = 0;     // ... and after it
  uint16_t unknownBefore = 0;  // siblings not counted yet, before / after the current file
  uint16_t unknownAfter = 0;
  uint32_t unknownBytesBefore = 0;
  uint32_t unknownBytesAfter = 0;
  uint32_t knownBytes = 0;    // bytes of the siblings counted exactly
  uint32_t currentBytes = 0;  // bytes of the current file

  struct Display {
    int page;
    int total;
    bool approximate;
  };

  // `page` is 1-based within the current file; `pageCount` is that file's total, which may itself
  // be a mid-build estimate. A pageCount of 0 means there is nothing to project from yet.
  Display apply(const int page, const int pageCount) const {
    if (pageCount <= 0) return {page, pageCount, false};
    const uint64_t pages = static_cast<uint64_t>(pagesBefore) + pagesAfter + static_cast<uint64_t>(pageCount);
    const uint64_t bytes = static_cast<uint64_t>(knownBytes) + currentBytes;
    const auto estimate = [pages, bytes](const uint32_t unknownBytes, const uint16_t files) -> int {
      if (files == 0) return 0;
      const uint64_t est = bytes > 0 ? (unknownBytes * pages + bytes / 2) / bytes : 0;
      return static_cast<int>(std::max<uint64_t>(est, files));
    };
    const int before = static_cast<int>(pagesBefore) + estimate(unknownBytesBefore, unknownBefore);
    const int after = static_cast<int>(pagesAfter) + estimate(unknownBytesAfter, unknownAfter);
    return {before + page, before + pageCount + after, unknownBefore > 0 || unknownAfter > 0};
  }
};
