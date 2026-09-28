#pragma once

#include <HalStorage.h>

#include <cstdint>

namespace ProgressiveJpegDc {

enum class Result : uint8_t {
  Ok,
  Unsupported,
  InvalidData,
  IoError,
  OutOfMemory,
  Aborted,
  Stopped,
};

using RowCallback = bool (*)(void* user, uint16_t y, const uint8_t* grayscale, uint16_t width);
using AbortCallback = bool (*)(void* user);

struct DecodeOptions {
  // Where the JPEG starts in the file (see probe()).
  uint32_t base = 0;
  uint16_t outputWidth = 0;
  uint16_t outputHeight = 0;
  AbortCallback shouldAbort = nullptr;
  void* abortUser = nullptr;
};

struct ImageInfo {
  uint16_t width = 0;
  uint16_t height = 0;
};

// Reads marker segments through SOF and rewinds the file before returning.
// Returns Unsupported for baseline and other JPEG coding modes.
// `base`: where the JPEG starts in the file -- 0 for a file of its own, the entry's data offset for
// an image stored uncompressed in a ZIP and read in place.
Result probe(FsFile& file, ImageInfo& info, uint32_t base = 0);

// Decodes the initial DC scan of an SOF2 JPEG into resized grayscale rows.
// The input file is rewound before decoding. No full-image coefficient or pixel
// buffer is allocated; unsupported scan scripts fail without consuming output.
Result decode(FsFile& file, const DecodeOptions& options, RowCallback rowCallback, void* rowUser);

const char* resultName(Result result);

}  // namespace ProgressiveJpegDc
