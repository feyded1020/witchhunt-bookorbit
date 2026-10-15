#pragma once

#include <algorithm>

// Which rows of a list to keep decoded, for a screen that reads its rows from storage a few at a
// time instead of holding them all (ReadingStatsBookListActivity). Pure, so the host suite pins it
// (test/list_row_cache/).
//
// The loop task knows the selection and a window: the one the last render published (top row, rows
// drawn), or the top a page turn has just set. The next render draws that window while the
// selection is in it. Otherwise the selection moved off it by steps, and the follow scrolls the
// least it can: the selection lands on the window's first line (it went up) or its last (it went
// down). Either way the next window holds the selection.
namespace ListRowCache {

// Rows [first, last).
struct Range {
  int first = 0;
  int last = 0;

  [[nodiscard]] bool holds(const Range& other) const { return other.first >= first && other.last <= last; }
};

namespace detail {

// The top of the window the next render draws, and the rows it draws (at least one). `count` > 0.
inline int nextTop(const int count, const int selected, const int windowTop, const int drawn) {
  const int sel = std::clamp(selected, 0, count - 1);
  const int top = std::clamp(windowTop, 0, count - 1);
  if (sel < top) return sel;
  if (sel >= top + drawn) return sel - drawn + 1;
  return top;
}

}  // namespace detail

// The rows the next render draws, and the one after them: list() asks the row provider for the row
// past the window to decide whether it fits, and on layouts whose row height follows the content
// a placeholder there could fit where the real row would not.
inline Range needed(const int count, const int selected, const int windowTop, const int windowDrawn) {
  if (count <= 0) return {};
  const int drawn = std::max(1, windowDrawn);
  const int top = detail::nextTop(count, selected, windowTop, drawn);
  return {top, std::min(count, top + drawn + 1)};
}

// The rows to decode once needed() is no longer held: that window and one window either side,
// (plus the one row needed() adds past a window), clipped to the list. Stepping then refills about once a screen, and a
// page turn about every other page, rather than on every press.
inline Range toDecode(const int count, const int selected, const int windowTop, const int windowDrawn) {
  if (count <= 0) return {};
  const int drawn = std::max(1, windowDrawn);
  const int top = detail::nextTop(count, selected, windowTop, drawn);
  return {std::max(0, top - drawn), std::min(count, top + 2 * drawn + 1)};
}

}  // namespace ListRowCache
