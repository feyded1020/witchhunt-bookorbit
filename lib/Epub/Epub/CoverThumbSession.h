#pragma once

#include <HalStorage.h>
#include <JpegToBmpConverter.h>

#include <cstdint>
#include <memory>
#include <string>

// An EPUB cover's thumbnails converted a few units at a time (memory audit 2026-09, R9 item 3).
// Home's cover pass runs a slice, checks for a button press, and comes back later to the same
// place; the one-shot conversion it replaces gave way to input by throwing the decode away and
// starting over, which for a 1.3 MB progressive cover was seconds lost per press.
//
// Created by Epub::beginThumbSession(). Owns the cover file and every thumbnail it writes. A
// session that goes unfinished or fails removes its partial thumbnails, so a half-written BMP
// never reads as a cover, and never as the 0-byte "no cover" sentinel either.
class CoverThumbSession {
 public:
  enum class Status : uint8_t { Running, Done, Error };

  // Up to `units` of decoding (see JpegThumbSession::continueSteps). Done: every thumbnail is
  // complete and closed. Error: the partial thumbnails are already removed.
  Status continueSteps(uint16_t units);

  int outputCount() const { return count_; }
  bool progressive() const;

  ~CoverThumbSession();
  CoverThumbSession(const CoverThumbSession&) = delete;
  CoverThumbSession& operator=(const CoverThumbSession&) = delete;

 private:
  friend class Epub;
  CoverThumbSession() = default;
  void removeOutputs();

  static constexpr int kMax = JpegToBmpConverter::kMaxTargets;
  FsFile cover_;
  FsFile thumbs_[kMax];
  std::string paths_[kMax];
  int count_ = 0;  // outputs opened so far; every one of them is removed unless the session is Done
  // Declared after the files it writes to: it flushes into them and rewinds the cover as it goes.
  std::unique_ptr<JpegThumbSession> jpeg_;
  Status status_ = Status::Running;
};
