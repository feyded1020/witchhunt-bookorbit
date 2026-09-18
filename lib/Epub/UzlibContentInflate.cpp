#include "UzlibContentInflate.h"

#include <cstring>

namespace {

// uzlib's read callback receives only the uzlib_uncomp* it lives in, with no context pointer —
// so the context is recovered by placing the InflateReader FIRST in the struct (its own first
// member is that uzlib_uncomp). This is the pattern InflateReader.h documents; it is the reason
// PullContext exists instead of putting these fields on UzlibContentInflate, which has a vtable
// pointer first and so cannot be cast to.
int pullFromSource(uzlib_uncomp* u) {
  auto* ctx = reinterpret_cast<UzlibContentInflate::PullContext*>(u);
  if (!ctx->source) return -1;
  const size_t got = ctx->source(ctx->context, ctx->buffer, sizeof(ctx->buffer));
  if (got == 0) return -1;  // genuine end of input; uzlib latches EOF from here
  // uzlib takes the first byte as the return value and reads the rest from source/source_limit.
  u->source = ctx->buffer + 1;
  u->source_limit = ctx->buffer + got;
  return ctx->buffer[0];
}

}  // namespace

bool UzlibContentInflate::begin(const size_t expectedOutputSize, const SourceFn source, void* context) {
  end();
  ctx_.source = source;
  ctx_.context = context;

  // The size hint is why this provider exists: a 3 KB chapter gets a 3 KB ring instead of the
  // fixed 32 KB dictionary miniz would allocate inside a single ~40 KB block. 0 means unknown,
  // which InflateReader reads as "give me the full window".
  if (!ctx_.reader.init(/*streaming=*/true, expectedOutputSize)) return false;
  ctx_.reader.setReadCallback(pullFromSource);
  active_ = true;
  ended_ = false;
  return true;
}

freeink::content::Inflate::Status UzlibContentInflate::read(uint8_t* out, const size_t outCap, size_t* produced) {
  if (produced) *produced = 0;
  if (!active_ || !out || outCap == 0) return Status::Error;
  if (ended_) return Status::StreamEnd;

  size_t got = 0;
  const InflateStatus status = ctx_.reader.readAtMost(out, outCap, &got);
  if (produced) *produced = got;

  switch (status) {
    case InflateStatus::Done:
      ended_ = true;
      return Status::StreamEnd;
    case InflateStatus::Ok:
      return Status::Ok;
    case InflateStatus::Error:
    default:
      return Status::Error;
  }
}

void UzlibContentInflate::end() {
  if (active_) ctx_.reader.deinit();
  ctx_.source = nullptr;
  ctx_.context = nullptr;
  active_ = false;
  ended_ = false;
}
