#pragma once

// The firmware's decompressor, handed to the content-protection library so its protected-entry
// reads go through the same uzlib every other inflate in this firmware uses.
//
// Two reasons this exists rather than letting the library use its own miniz:
//
//   * it would be a SECOND inflate implementation in the image, for one feature;
//   * miniz's inflate state is one ~40 KB allocation (tinfl plus a fixed 32 KB dictionary),
//     and a single large contiguous block is the hardest thing to get on this heap —
//     docs/memory-allocation-strategy.md exists because of exactly that failure mode.
//
// InflateReader sizes its back-reference ring to min(32 KB, expectedOutputSize), so a 3 KB
// chapter costs a 3 KB ring. That is the whole point of taking the size hint.
//
// Not thread-safe and not re-entrant: one instance decompresses one stream at a time, which is
// how ProtectedBook uses it.

#include <Inflate.h>
#include <InflateReader.h>

#include <cstdint>

class UzlibContentInflate final : public freeink::content::Inflate {
 public:
  ~UzlibContentInflate() override { end(); }

  bool begin(size_t expectedOutputSize, SourceFn source, void* context) override;
  Status read(uint8_t* out, size_t outCap, size_t* produced) override;
  void end() override;

  // uzlib's read callback gets only the uzlib_uncomp* it belongs to — no context argument — so
  // the context is recovered by putting the reader FIRST here and casting back (InflateReader's
  // own first member is that uzlib_uncomp). That is why these fields live in a plain struct
  // rather than on the class, which has a vtable pointer first and cannot be cast to.
  struct PullContext {
    InflateReader reader;  // MUST be first
    SourceFn source = nullptr;
    void* context = nullptr;
    // Staging for one pull. 512 B keeps the frame small; the callback simply runs again.
    uint8_t buffer[512] = {};
  };

 private:
  PullContext ctx_;
  bool active_ = false;
  bool ended_ = false;
};
