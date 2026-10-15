#include "FolderCountMemo.h"

#include <algorithm>

namespace {

// FNV-1a: with 64 entries told apart by hash and length together, a collision would take two
// same-length paths among a handful of folders hashing alike.
uint32_t hashPath(const std::string_view path) {
  uint32_t hash = 2166136261u;
  for (const char c : path) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

uint16_t lengthOf(const std::string_view path) { return static_cast<uint16_t>(std::min<size_t>(path.size(), 0xFFFF)); }

}  // namespace

void FolderCountMemo::adopt(const uint32_t stamp) {
  if (stamp == stamp_) return;
  clear();
  stamp_ = stamp;
}

FolderCountMemo::Entry* FolderCountMemo::lookup(const uint32_t hash, const uint16_t length) {
  for (size_t i = 0; i < used_; ++i) {
    if (entries_[i].hash == hash && entries_[i].length == length) return &entries_[i];
  }
  return nullptr;
}

int FolderCountMemo::find(const std::string_view folder, const uint32_t stamp) {
  adopt(stamp);
  const Entry* entry = lookup(hashPath(folder), lengthOf(folder));
  return entry != nullptr ? entry->books : -1;
}

void FolderCountMemo::store(const std::string_view folder, const int books, const uint32_t stamp) {
  if (books < 0) return;
  adopt(stamp);
  const uint32_t hash = hashPath(folder);
  const uint16_t length = lengthOf(folder);
  const auto count = static_cast<int16_t>(std::min(books, 0x7FFF));
  if (Entry* existing = lookup(hash, length)) {
    existing->books = count;
    return;
  }
  if (used_ < CAPACITY) {
    entries_[used_++] = Entry{hash, length, count};
    return;
  }
  entries_[next_] = Entry{hash, length, count};
  next_ = (next_ + 1) % CAPACITY;
}

void FolderCountMemo::clear() {
  used_ = 0;
  next_ = 0;
}
