#pragma once

// WitchReader SecureNet — a download into a sink: one streamed response, or Range chunks when the
// heap is too tight for 16 KB TLS records (HttpRange.h has why and the sizing).
//
// HttpDownloader's loop, kept here so it builds without the firmware around it: test/range_download
// drives it over a scripted network.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "HttpRange.h"
#include "SecureHttpClient.h"

namespace crosspoint {

struct DownloadSink {
  // Returns false to abort the transfer (an SD write failure or a user cancel).
  std::function<bool(const uint8_t* data, size_t len)> write;
  // Drops everything written so far. Set only for file downloads, which are the only transfers
  // fetched in chunks: a chunk lands where the previous one ended, and a server that ignores Range
  // restarts the body from byte 0.
  std::function<bool()> rewind;
  // Whole-file progress (downloaded, total), once the total is known. Return false to abort.
  std::function<bool(size_t downloaded, size_t total)> progress;
  size_t total = 0;
  size_t downloaded = 0;
};

// What one session learns about chunked downloads: an HttpDownloader::Session across its files, or a
// single one-shot download.
struct ChunkSession {
  // The largest chunk its files may use. A size that ran out of memory reading a record stays out of
  // reach for the rest of the session.
  size_t ceiling = http_range::DEFAULT_CHUNK_BYTES;
  // A server answered a Range request with 200: the session streams from then on, with the one
  // attempt per file that streaming always had.
  bool rangeUnsupported = false;
};

// The sink could not be rewound (the file could not be reopened). A SecureHttpError-style code.
constexpr int ERR_REWIND = -20;
// The file's size changed between two chunks: a new version appeared on the server mid-download.
constexpr int ERR_RESOURCE_CHANGED = -21;

using LargestFreeBlockFn = size_t (*)();

// Downloads url into sink over http (a kept-alive connection is reused). File downloads (sink.rewind
// set) over https are fetched in Range chunks when largestFreeBlock(), read once the connection is
// up, is below http_range::STREAM_MIN_LARGEST_BLOCK; everything else streams as one response.
// Returns what SecureHttpClient::get() does: the HTTP status (200 for a complete chunked download)
// or a negative SecureHttpError, or ERR_REWIND / ERR_RESOURCE_CHANGED.
int downloadToSink(SecureHttpClient& http, const std::string& url, DownloadSink& sink, ChunkSession& session,
                   LargestFreeBlockFn largestFreeBlock);

}  // namespace crosspoint
