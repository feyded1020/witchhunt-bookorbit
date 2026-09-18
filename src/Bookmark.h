#pragma once

#include <cstdint>
#include <string>

struct Bookmark {
  uint16_t spineIndex;
  uint16_t pageNumber;
  std::string name;  // optional user-provided label (empty = use default)

  // BookOrbit sync (file version 3). Layout-independent identity and position: page numbers
  // move with font and margin changes, so sync works from these instead.
  static constexpr uint16_t PROGRESS_UNKNOWN = 0xFFFF;
  static constexpr uint8_t FLAG_PAGE_APPROX = 0x01;  // pageNumber still to be resolved from progressQ

  uint32_t timestamp = 0;                 // creation time, UTC epoch seconds (0 = unknown / pre-v3)
  uint16_t progressQ = PROGRESS_UNKNOWN;  // position within the chapter, 0..10000
  uint8_t flags = 0;
};
