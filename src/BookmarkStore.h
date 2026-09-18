#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "Bookmark.h"

// Stores starred/bookmarked pages for a single book.
// Persisted as a binary file on SD card within the book's cache directory.
class BookmarkStore {
 public:
  using Bookmark = ::Bookmark;

  // Load bookmarks from the cache directory (e.g. .crosspoint/epub_<hash>/).
  void load(const std::string& cachePath) {
    basePath = cachePath;
    bookmarks.clear();
    dirty = false;

    FsFile f;
    if (!Storage.openFileForRead("BKM", getFilePath(), f)) {
      return;
    }

    uint8_t version;
    if (f.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) != sizeof(version) || version < 1 ||
        version > FILE_VERSION) {
      f.close();
      return;
    }

    uint16_t count;
    if (f.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) != sizeof(count) || count > MAX_BOOKMARKS) {
      LOG_ERR("BKM", "Invalid bookmark count: %u", static_cast<unsigned>(count));
      f.close();
      return;
    }

    bookmarks.reserve(count);
    for (uint16_t i = 0; i < count; i++) {
      Bookmark bm;
      if (f.read(reinterpret_cast<uint8_t*>(&bm.spineIndex), sizeof(bm.spineIndex)) != sizeof(bm.spineIndex) ||
          f.read(reinterpret_cast<uint8_t*>(&bm.pageNumber), sizeof(bm.pageNumber)) != sizeof(bm.pageNumber)) {
        LOG_ERR("BKM", "Truncated bookmarks file at entry %d", i);
        bookmarks.clear();
        f.close();
        return;
      }
      if (version >= 2) {
        uint16_t nameLen = 0;
        if (f.read(reinterpret_cast<uint8_t*>(&nameLen), sizeof(nameLen)) != sizeof(nameLen) ||
            nameLen > MAX_NAME_LENGTH) {
          LOG_ERR("BKM", "Invalid bookmark name length at entry %d", i);
          bookmarks.clear();
          f.close();
          return;
        }
        if (nameLen > 0) {
          bm.name.resize(nameLen);
          if (f.read(reinterpret_cast<uint8_t*>(&bm.name[0]), nameLen) != nameLen) {
            LOG_ERR("BKM", "Truncated bookmark name at entry %d", i);
            bookmarks.clear();
            f.close();
            return;
          }
        }
      }
      if (version >= 3) {
        if (f.read(reinterpret_cast<uint8_t*>(&bm.timestamp), sizeof(bm.timestamp)) != sizeof(bm.timestamp) ||
            f.read(reinterpret_cast<uint8_t*>(&bm.progressQ), sizeof(bm.progressQ)) != sizeof(bm.progressQ) ||
            f.read(reinterpret_cast<uint8_t*>(&bm.flags), sizeof(bm.flags)) != sizeof(bm.flags)) {
          LOG_ERR("BKM", "Truncated bookmark sync fields at entry %d", i);
          bookmarks.clear();
          f.close();
          return;
        }
      }
      bookmarks.push_back(std::move(bm));
    }

    f.close();
    LOG_DBG("BKM", "Loaded %d bookmarks", count);
  }

  // Save bookmarks to SD card (only if changed).
  void save() {
    if (!dirty || basePath.empty()) {
      return;
    }

    if (bookmarks.size() > MAX_BOOKMARKS) {
      LOG_ERR("BKM", "Too many bookmarks to save: %u", static_cast<unsigned>(bookmarks.size()));
      return;
    }

    FsFile f;
    if (!Storage.openFileForWrite("BKM", getFilePath(), f)) {
      LOG_ERR("BKM", "Failed to save bookmarks");
      return;
    }

    auto writePodChecked = [&f](const auto& value) {
      return f.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
    };

    const uint16_t count = static_cast<uint16_t>(bookmarks.size());
    bool ok = writePodChecked(FILE_VERSION) && writePodChecked(count);

    for (const auto& bm : bookmarks) {
      const uint16_t nameLen = static_cast<uint16_t>(std::min<size_t>(bm.name.size(), MAX_NAME_LENGTH));
      ok = ok && writePodChecked(bm.spineIndex) && writePodChecked(bm.pageNumber) && writePodChecked(nameLen);
      if (ok && nameLen > 0) {
        ok = f.write(reinterpret_cast<const uint8_t*>(bm.name.data()), nameLen) == nameLen;
      }
      ok = ok && writePodChecked(bm.timestamp) && writePodChecked(bm.progressQ) && writePodChecked(bm.flags);
    }

    if (ok) {
      if (!f.close()) {
        LOG_ERR("BKM", "Failed to close bookmarks file");
        return;
      }
    } else {
      f.close();
      LOG_ERR("BKM", "Failed while writing bookmarks");
      return;
    }
    dirty = false;
    LOG_DBG("BKM", "Saved %d bookmarks", count);
  }

  // Toggle bookmark for the given page. Returns true if now starred, false if removed.
  // `pageCount` (the chapter's page count, when known) records the layout-independent
  // position BookOrbit sync needs; 0 leaves it to be filled in when the chapter is next open.
  bool toggle(uint16_t spineIndex, uint16_t pageNumber, uint16_t pageCount = 0) {
    auto it = find(spineIndex, pageNumber);
    if (it != bookmarks.end()) {
      bookmarks.erase(it);
      dirty = true;
      return false;
    }
    Bookmark bm{spineIndex, pageNumber, {}};
    bm.timestamp = uniqueTimestamp(nowEpochOrZero());
    bm.progressQ = progressFor(pageNumber, pageCount);
    bookmarks.push_back(std::move(bm));
    dirty = true;
    return true;
  }

  // Adds a bookmark received from BookOrbit. Its page is provisional (chapter start) until
  // resolvePagesForChapter() runs with that chapter's real page count. Returns the stored
  // timestamp (its sync identity), or 0 when no plausible clock exists to mint one.
  uint32_t addSynced(uint16_t spineIndex, uint16_t progressQ, std::string name) {
    const uint32_t ts = uniqueTimestamp(nowEpochOrZero());
    if (ts == 0 || bookmarks.size() >= MAX_BOOKMARKS) return 0;
    Bookmark bm{spineIndex, 0, std::move(name)};
    if (bm.name.size() > MAX_NAME_LENGTH) bm.name.resize(MAX_NAME_LENGTH);
    bm.timestamp = ts;
    bm.progressQ = progressQ;
    bm.flags = Bookmark::FLAG_PAGE_APPROX;
    bookmarks.push_back(std::move(bm));
    dirty = true;
    return ts;
  }

  // Called by the reader once a chapter's page count is known: places synced bookmarks on
  // their real page, and records the position of local ones created without it. Also gives
  // pre-v3 bookmarks a sync identity. Returns true if anything changed.
  bool resolvePagesForChapter(uint16_t spineIndex, uint16_t pageCount) {
    if (pageCount == 0) return false;
    bool changed = false;
    for (auto& bm : bookmarks) {
      if (bm.spineIndex != spineIndex) continue;
      if ((bm.flags & Bookmark::FLAG_PAGE_APPROX) && bm.progressQ != Bookmark::PROGRESS_UNKNOWN) {
        const uint32_t last = pageCount > 1 ? pageCount - 1u : 0u;
        bm.pageNumber = static_cast<uint16_t>((static_cast<uint32_t>(bm.progressQ) * last + 5000u) / 10000u);
        bm.flags &= static_cast<uint8_t>(~Bookmark::FLAG_PAGE_APPROX);
        changed = true;
      } else if (bm.progressQ == Bookmark::PROGRESS_UNKNOWN && bm.pageNumber < pageCount) {
        bm.progressQ = progressFor(bm.pageNumber, pageCount);
        changed = true;
      }
      if (bm.timestamp == 0) {
        const uint32_t ts = uniqueTimestamp(nowEpochOrZero());
        if (ts != 0) {
          bm.timestamp = ts;
          changed = true;
        }
      }
    }
    if (changed) dirty = true;
    return changed;
  }

  // Removes the bookmark with this sync identity. Returns true if one was removed.
  bool removeByTimestamp(uint32_t timestamp) {
    if (timestamp == 0) return false;
    auto it = std::find_if(bookmarks.begin(), bookmarks.end(),
                           [timestamp](const Bookmark& bm) { return bm.timestamp == timestamp; });
    if (it == bookmarks.end()) return false;
    bookmarks.erase(it);
    dirty = true;
    return true;
  }

  // Check if a page is starred.
  [[nodiscard]] bool has(uint16_t spineIndex, uint16_t pageNumber) const {
    return std::any_of(bookmarks.begin(), bookmarks.end(), [spineIndex, pageNumber](const Bookmark& bm) {
      return bm.spineIndex == spineIndex && bm.pageNumber == pageNumber;
    });
  }

  [[nodiscard]] const std::vector<Bookmark>& getAll() const { return bookmarks; }
  [[nodiscard]] bool isEmpty() const { return bookmarks.empty(); }
  void markDirty() { dirty = true; }

  // Set or clear the name for the bookmark at index. Empty name reverts to default label.
  void rename(size_t index, std::string name) {
    if (index >= bookmarks.size()) return;
    if (name.size() > MAX_NAME_LENGTH) name.resize(MAX_NAME_LENGTH);
    bookmarks[index].name = std::move(name);
    dirty = true;
  }

  void removeAt(size_t index) {
    if (index >= bookmarks.size()) return;
    bookmarks.erase(bookmarks.begin() + index);
    dirty = true;
  }

  static constexpr uint16_t MAX_NAME_LENGTH = 128;

 private:
  static constexpr uint8_t FILE_VERSION = 3;
  static constexpr uint16_t MAX_BOOKMARKS = 1000;

  std::vector<Bookmark> bookmarks;
  std::string basePath;
  bool dirty = false;

  [[nodiscard]] std::string getFilePath() const { return basePath + "/bookmarks.bin"; }

  static uint32_t nowEpochOrZero() {
    const time_t now = time(nullptr);
    return now >= 1577836800 ? static_cast<uint32_t>(now) : 0;  // 2020-01-01: clock never set
  }

  // Timestamps double as sync identities, so two bookmarks made in the same second must differ.
  uint32_t uniqueTimestamp(uint32_t ts) const {
    if (ts == 0) return 0;
    bool clash = true;
    while (clash) {
      clash = std::any_of(bookmarks.begin(), bookmarks.end(), [ts](const Bookmark& bm) { return bm.timestamp == ts; });
      if (clash) ts++;
    }
    return ts;
  }

  static uint16_t progressFor(uint16_t pageNumber, uint16_t pageCount) {
    if (pageCount == 0) return Bookmark::PROGRESS_UNKNOWN;
    if (pageCount == 1) return 0;
    const uint32_t q = (static_cast<uint32_t>(pageNumber) * 10000u + (pageCount - 1u) / 2u) / (pageCount - 1u);
    return static_cast<uint16_t>(std::min<uint32_t>(q, 10000u));
  }

  std::vector<Bookmark>::iterator find(uint16_t spineIndex, uint16_t pageNumber) {
    return std::find_if(bookmarks.begin(), bookmarks.end(), [spineIndex, pageNumber](const Bookmark& bm) {
      return bm.spineIndex == spineIndex && bm.pageNumber == pageNumber;
    });
  }
};
