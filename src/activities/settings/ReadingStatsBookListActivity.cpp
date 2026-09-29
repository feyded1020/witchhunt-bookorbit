#include "ReadingStatsBookListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "ReadingStatsBookDetailActivity.h"
#include "components/UITheme.h"
#include "components/themes/ListTouchBand.h"
#include "fontIds.h"

namespace {

// Same compact format as the main stats screen — kept inline rather than
// shared via a header to keep this slice's footprint small. If a third
// caller appears we'll lift it into a util.
std::string formatDuration(uint32_t totalSeconds) {
  const uint32_t h = totalSeconds / 3600;
  const uint32_t m = (totalSeconds % 3600) / 60;
  const uint32_t s = totalSeconds % 60;
  char buf[24];
  if (h > 0) {
    snprintf(buf, sizeof(buf), "%uh %02um", h, m);
  } else if (m > 0) {
    snprintf(buf, sizeof(buf), "%um %02us", m, s);
  } else {
    snprintf(buf, sizeof(buf), "%us", s);
  }
  return buf;
}

}  // namespace

void ReadingStatsBookListActivity::rebuildIndex() {
  ReadingStatsStore::Summary summary;
  if (READING_STATS.querySummary(summary, /*withIndex=*/true) != ReadingStatsStore::ReadResult::Ok) {
    summary.byTime.clear();
  }
  RenderLock lock(*this);
  index_ = std::move(summary.byTime);
  indexSeq_ = summary.seq;
  rows_.clear();
  rowsFirst_ = 0;
  selectedIndex = std::min(selectedIndex, std::max(0, static_cast<int>(index_.size()) - 1));
}

// The rows BaseTheme::drawList() / LyraTheme::drawList() will draw: pages of
// min(height / rowHeight, ListTouchBand::kMaxRows) rows, the one holding the selection.
int ReadingStatsBookListActivity::pageItems() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, /*hasBottomHints=*/true, /*hasSideHints=*/false);
  const int contentHeight =
      contentRect.height - (metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2);
  return std::max(1, std::min(contentHeight / metrics.listWithSubtitleRowHeight, ListTouchBand::kMaxRows));
}

bool ReadingStatsBookListActivity::readRows(const int first, const int page,
                                            std::vector<BookReadingStats>& rows) const {
  const int last = std::min(first + page, static_cast<int>(index_.size()));
  const auto count = static_cast<size_t>(std::max(0, last - first));
  const auto result = READING_STATS.queryBooksAt(index_, static_cast<size_t>(first), count, indexSeq_, rows);
  if (result == ReadingStatsStore::ReadResult::Stale) return false;
  if (result != ReadingStatsStore::ReadResult::Ok) rows.assign(count, BookReadingStats{});
  for (BookReadingStats& row : rows) row.days.clear();  // a row shows title, author, time and the finished mark
  return true;
}

void ReadingStatsBookListActivity::ensureRowsFor(const int index) {
  if (index_.empty()) return;
  const int page = pageItems();
  int first = index / page * page;
  if (first == rowsFirst_ && !rows_.empty()) return;
  std::vector<BookReadingStats> rows;
  if (!readRows(first, page, rows)) {
    // The history changed under the list. Take the order again, once.
    rebuildIndex();
    first = selectedIndex / page * page;
    if (!readRows(first, page, rows)) rows.clear();
  }
  RenderLock lock(*this);
  rows_ = std::move(rows);
  rowsFirst_ = first;
}

const BookReadingStats* ReadingStatsBookListActivity::rowAt(const int index) const {
  const int at = index - rowsFirst_;
  return at >= 0 && at < static_cast<int>(rows_.size()) ? &rows_[static_cast<size_t>(at)] : nullptr;
}

void ReadingStatsBookListActivity::onEnter() {
  Activity::onEnter();
  rebuildIndex();
  ensureRowsFor(selectedIndex);
  requestUpdate();
}

void ReadingStatsBookListActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (!index_.empty()) {
    const int count = static_cast<int>(index_.size());
    buttonNavigator.onNextList(selectedIndex, count, [this]() {
      ensureRowsFor(selectedIndex);
      requestUpdate();
    });
    buttonNavigator.onPreviousList(selectedIndex, count, [this]() {
      ensureRowsFor(selectedIndex);
      requestUpdate();
    });

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      const BookReadingStats* row = rowAt(selectedIndex);
      if (row == nullptr || row->docId.empty()) return;
      startActivityForResult(std::make_unique<ReadingStatsBookDetailActivity>(renderer, mappedInput, row->docId),
                             [this](const ActivityResult&) {
                               // The detail screen may have removed its book: read the order again,
                               // keeping the selection on the same row where there still is one.
                               rebuildIndex();
                               ensureRowsFor(selectedIndex);
                               requestUpdate();
                             });
    }
  }
}

void ReadingStatsBookListActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, /*hasBottomHints=*/true, /*hasSideHints=*/false);

  renderer.clearScreen();

  GUI.drawHeader(renderer,
                 Rect{contentRect.x, contentRect.y + metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_READING_STATS_BOOK_LIST), nullptr);

  const int contentTop = contentRect.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      contentRect.height - (metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2);

  if (index_.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, tr(STR_READING_STATS_NO_DATA));
  } else {
    GUI.drawList(
        renderer, Rect{contentRect.x, contentTop, contentRect.width, contentHeight}, static_cast<int>(index_.size()),
        selectedIndex,
        [this](int index) {
          const BookReadingStats* row = rowAt(index);
          if (row == nullptr) return std::string("…");
          // Title is the primary label; fall back to docId so a row without
          // metadata is still recognizable. Finished books get a leading
          // checkmark so the user can spot completions at a glance.
          std::string label = row->title.empty() ? row->docId : row->title;
          if (row->finishedCount > 0) label = "✓ " + label;
          return label;
        },
        [this](int index) {
          // Subtitle row: author, when known. Empty string is treated by the
          // theme as "no subtitle" and the row collapses to a single line.
          const BookReadingStats* row = rowAt(index);
          return row == nullptr ? std::string() : row->author;
        },
        nullptr,
        [this](int index) {
          const BookReadingStats* row = rowAt(index);
          return row == nullptr ? std::string() : formatDuration(row->totalSeconds);
        },
        true);
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), index_.empty() ? "" : tr(STR_SELECT), index_.empty() ? "" : tr(STR_DIR_UP),
                            index_.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

ListRowTap::Result ReadingStatsBookListActivity::selectListRow(const int index) {
  const ListRowTap::Result result = ListRowTap::apply(index, static_cast<int>(index_.size()), selectedIndex);
  ensureRowsFor(selectedIndex);
  return result;
}
