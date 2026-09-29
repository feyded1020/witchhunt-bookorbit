#pragma once

#include "ReadingStats.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Phase-2 sub-screen: scrollable list of all books with recorded reading time.
// Sorted by total time descending so the most-read books are easiest to reach.
// Selecting a row pushes ReadingStatsBookDetailActivity.
//
// The history is never loaded: one scan of the stats file gives the order by time (8 bytes a
// book), and only the rows of the page on screen are decoded, each read at its offset.
class ReadingStatsBookListActivity final : public Activity {
 public:
  explicit ReadingStatsBookListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStatsBookList", renderer, mappedInput) {}

  void onEnter() override;
  // Tap on a list row -> move the selection there; ActivityManager then synthesizes Confirm.
  ListRowTap::Result selectListRow(int index) override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Rows are decoded on the loop task and swapped in under RenderLock; render() only reads them.
  std::vector<ReadingStatsStore::IndexEntry> index_;
  uint32_t indexSeq_ = 0;  // the history's generation index_ was taken at
  std::vector<BookReadingStats> rows_;
  int rowsFirst_ = 0;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void rebuildIndex();
  void ensureRowsFor(int index);
  // Rows [first, first + page) of the index; false when the history changed since it was taken.
  bool readRows(int first, int page, std::vector<BookReadingStats>& rows) const;
  int pageItems() const;
  const BookReadingStats* rowAt(int index) const;
};
