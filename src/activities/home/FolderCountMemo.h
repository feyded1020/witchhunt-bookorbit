#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The book counts of the folders counted lately -- a folder card's number -- so a folder is not
// walked again each time it comes on screen. Counting a folder means listing every folder below
// it, which on a large library takes long enough to show "..." for a while.
//
// A fixed table of CAPACITY entries, 8 bytes each, whatever the size of the library: a folder is
// known by a hash of its path and the path's length, not by the path itself. When the table is
// full the oldest entry goes. Counting a folder records the folders below it on the way (see
// FileBrowserModel::countBooksBelow), deepest first, so after a large walk what is left is the
// folder asked about and the folders nearest it.
//
// Every entry belongs to one `stamp`, which the caller derives from whatever a count depends on --
// what is on the card (HalStorage::contentGeneration) and the rules it was counted by (whether
// hidden entries are listed). A different stamp empties the table: a count from before a change
// is not shown after it.
//
// Pure bookkeeping, no storage access; exercised on the host.
class FolderCountMemo {
 public:
  static constexpr size_t CAPACITY = 64;

  // The count recorded for `folder` under `stamp`, or -1.
  int find(std::string_view folder, uint32_t stamp);
  // Records `books` for `folder` under `stamp`, replacing an earlier count for it.
  void store(std::string_view folder, int books, uint32_t stamp);
  void clear();

  size_t size() const { return used_; }

 private:
  struct Entry {
    uint32_t hash;
    uint16_t length;
    int16_t books;
  };

  void adopt(uint32_t stamp);
  Entry* lookup(uint32_t hash, uint16_t length);

  std::array<Entry, CAPACITY> entries_{};
  size_t used_ = 0;
  size_t next_ = 0;  // the slot the next new entry takes once the table is full
  uint32_t stamp_ = 0;
};
