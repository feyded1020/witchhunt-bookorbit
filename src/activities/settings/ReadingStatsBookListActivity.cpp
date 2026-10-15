#include "ReadingStatsBookListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>

#include "ReadingStatsBookDetailActivity.h"
#include "util/ListRowCache.h"

namespace fui = freeink::ui;

namespace {

// Same compact format as the main stats screen ("1h 05m", "3m 07s", "42s") — kept inline rather
// than shared via a header to keep this slice's footprint small. If a third caller appears we'll
// lift it into a util.
void formatDuration(const uint32_t totalSeconds, char* out, const size_t outSize) {
  const uint32_t h = totalSeconds / 3600;
  const uint32_t m = (totalSeconds % 3600) / 60;
  const uint32_t s = totalSeconds % 60;
  if (h > 0) {
    snprintf(out, outSize, "%uh %02um", h, m);
  } else if (m > 0) {
    snprintf(out, outSize, "%um %02us", m, s);
  } else {
    snprintf(out, outSize, "%us", s);
  }
}

}  // namespace

void ReadingStatsBookListActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildIndex();
  // The list opens on its first row, before any render has published a window.
  ensureRowsFor(0, 0);
}

const char* ReadingStatsBookListActivity::headerTitle() const { return tr(STR_READING_STATS_BOOK_LIST); }

const char* ReadingStatsBookListActivity::footerConfirmLabel() const {
  return index_.empty() ? "" : UiListActivity::footerConfirmLabel();
}

void ReadingStatsBookListActivity::rebuildIndex() {
  ReadingStatsStore::Summary summary;
  if (READING_STATS.querySummary(summary, /*withIndex=*/true) != ReadingStatsStore::ReadResult::Ok) {
    summary.byTime.clear();
  }
  {
    RenderLock lock(*this);
    index_ = std::move(summary.byTime);
    indexSeq_ = summary.seq;
    rows_.clear();
    rowsFirst_ = 0;
  }
  // Keep the selection on a row that still exists: the detail screen may have removed the last
  // book. The selection is atomic, and the next build follows it.
  auto& current = activeNav();
  const int last = std::max(0, static_cast<int>(index_.size()) - 1);
  if (current.selected.load() > last) current.requestSelection(last);
  requestUpdate();
}

int ReadingStatsBookListActivity::windowRows() const {
  // UiListActivity::onEnter() resets the published window to one row; a render replaces it.
  const int drawn = publishedWindow().drawn;
  return drawn > 1 ? drawn : kOpeningRows;
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

// Loop task only. `windowTop` is where the next render starts as far as the loop task knows: the
// top the last render published, or the top a page turn has just set.
void ReadingStatsBookListActivity::ensureRowsFor(const int selected, const int windowTop) {
  if (index_.empty()) return;
  const int drawn = windowRows();
  const ListRowCache::Range held{rowsFirst_, rowsFirst_ + static_cast<int>(rows_.size())};
  if (held.holds(ListRowCache::needed(static_cast<int>(index_.size()), selected, windowTop, drawn))) return;

  ListRowCache::Range range = ListRowCache::toDecode(static_cast<int>(index_.size()), selected, windowTop, drawn);
  std::vector<BookReadingStats> rows;
  if (!readRows(range.first, range.last - range.first, rows)) {
    // The history changed under the list. Take the order again, once.
    rebuildIndex();
    if (index_.empty()) return;
    range = ListRowCache::toDecode(static_cast<int>(index_.size()), activeNav().selected.load(), windowTop, drawn);
    if (!readRows(range.first, range.last - range.first, rows)) {
      // Still changing. Hold blank rows rather than read again on every tick; the next window
      // that leaves them reads afresh.
      rows.assign(static_cast<size_t>(range.last - range.first), BookReadingStats{});
    }
  }
  {
    RenderLock lock(*this);
    rows_ = std::move(rows);
    rowsFirst_ = range.first;
  }
  requestUpdate();
}

const BookReadingStats* ReadingStatsBookListActivity::rowAt(const int index) const {
  const int at = index - rowsFirst_;
  return at >= 0 && at < static_cast<int>(rows_.size()) ? &rows_[static_cast<size_t>(at)] : nullptr;
}

bool ReadingStatsBookListActivity::handleCustomInput() {
  // The first frame, a follow that scrolled further than the selection change predicted, or a
  // window that grew (rows without an author fit more): the render drew rows the loop task never
  // decoded, as "…". Decode them here, on the loop task; the render only reads. When the rows are
  // held this is a range compare.
  ensureRowsFor(activeNav().selected.load(), publishedWindow().top);
  return false;
}

void ReadingStatsBookListActivity::onSelectionChanged(const int index) {
  ensureRowsFor(index, pageTop_ >= 0 ? pageTop_ : publishedWindow().top);
}

void ReadingStatsBookListActivity::showPositionPage(const int position, const int topPosition) {
  // The published window is the last render's. A page turn moves the screen by a whole window, so
  // checking the rows against the old one would miss the new screen's far end on every other page.
  pageTop_ = topPosition;
  UiListActivity::showPositionPage(position, topPosition);
  pageTop_ = -1;
}

void ReadingStatsBookListActivity::activateIndex(const int index) {
  const BookReadingStats* row = rowAt(index);
  if (row == nullptr || row->docId.empty()) return;  // not decoded, or a book the history no longer has
  app.clearTapFlash();
  startActivityForResult(std::make_unique<ReadingStatsBookDetailActivity>(renderer, mappedInput, row->docId),
                         [this](const ActivityResult&) {
                           // The detail screen may have removed its book: read the order again,
                           // keeping the selection on the same row where there still is one.
                           rebuildIndex();
                           ensureRowsFor(activeNav().selected.load(), publishedWindow().top);
                         });
}

void ReadingStatsBookListActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<ReadingStatsBookListActivity*>(ctx);
  item.actionValue = static_cast<int16_t>(index);
  const BookReadingStats* row = self->rowAt(index);
  if (row == nullptr) {
    // Not decoded yet: the loop task's next check reads it and repaints.
    item.label = "…";
    return;
  }
  // Title is the primary label; fall back to docId so a row without metadata is still
  // recognizable. Finished books get a leading checkmark so completions stand out.
  const std::string& name = row->title.empty() ? row->docId : row->title;
  if (row->finishedCount > 0) {
    const int written = snprintf(self->labelBuf_, sizeof(self->labelBuf_), "✓ %s", name.c_str());
    if (written >= static_cast<int>(sizeof(self->labelBuf_))) {
      // Cut on a character boundary, never inside a multi-byte one.
      self->labelBuf_[utf8SafeTruncateBuffer(self->labelBuf_, static_cast<int>(sizeof(self->labelBuf_)) - 1)] = '\0';
    }
    item.label = self->labelBuf_;
  } else {
    item.label = name.c_str();
  }
  // The author, when known; without one the row is a single line.
  if (!row->author.empty()) item.subtitle = row->author.c_str();
  formatDuration(row->totalSeconds, self->valueBuf_, sizeof(self->valueBuf_));
  item.value = self->valueBuf_;
}

void ReadingStatsBookListActivity::buildScreen(UiScreen& screen) {
  layoutListArea(screen);

  if (index_.empty()) {
    screen.centeredText(tr(STR_READING_STATS_NO_DATA), screen.theme().bodyText);
    return;
  }

  fui::ListProps props = listProps(screen);
  props.labelText = {};  // default label style, not the body-text preset
  props.rowProvider = &ReadingStatsBookListActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(index_.size());
  addList(screen, props, /*hasSubtitle=*/true);
}
