// Equivalence tests for the injected decompressor seam — docs/protected-content-plan.md §4.
//
// ContentProtection used to call miniz directly. It now pulls compressed bytes through an
// injected Inflate (freeink-sdk .../ContentProtection/include/Inflate.h) so this firmware can
// hand it the uzlib it already carries, instead of a second inflate implementation whose state
// wants one ~40 KB contiguous block.
//
// That swap is only safe if the two providers are interchangeable, so these tests drive BOTH
// over the same deflate stream and require identical output. The stream is small but heavily
// back-referenced, which is what exercises the sliding window across source refills — the part
// most likely to break when a push-shaped loop is inverted into a pull-shaped one.
//
// What this does NOT cover: ProtectedBook's own three loops, which need a protected book and a
// crypto backend. Those remain unproven until a real DRM'd EPUB is available.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "MinizInflate.h"
#include "UzlibContentInflate.h"

namespace {

using freeink::content::Inflate;

// Raw deflate (windowBits -15) of the text expectedPlain() rebuilds below.
const uint8_t kDeflateStream[] = {
    0xed, 0xd8, 0x4d, 0x4e, 0xc3, 0x30, 0x14, 0x45, 0xe1, 0xad, 0xbc, 0x05, 0x20, 0x84, 0x9f, 0x6d,
    0xfe, 0xa6, 0x5d, 0x89, 0x69, 0x4c, 0x03, 0x84, 0x24, 0x04, 0x97, 0x52, 0x56, 0x0f, 0x48, 0x2c,
    0xe1, 0x58, 0x4c, 0xee, 0x38, 0xd1, 0x1d, 0xe5, 0x93, 0xe3, 0xb3, 0x1b, 0xcb, 0xda, 0xea, 0x66,
    0x57, 0xf7, 0xd6, 0xc6, 0x6a, 0x6f, 0xc7, 0xa7, 0xfd, 0x8b, 0x3d, 0x6c, 0xcb, 0x69, 0xb6, 0xc7,
    0xe5, 0xd3, 0x9e, 0x8f, 0xaf, 0xeb, 0xbb, 0x2d, 0x1f, 0x3f, 0x6f, 0xfc, 0x3e, 0x9e, 0xca, 0xd7,
    0xd9, 0x86, 0xe5, 0x70, 0x61, 0x5b, 0x5d, 0x6b, 0x69, 0x75, 0x98, 0xce, 0x56, 0xe6, 0xc1, 0x4a,
    0xb3, 0xa9, 0xce, 0x87, 0x36, 0x5e, 0xda, 0xee, 0x6f, 0x31, 0xe0, 0x8b, 0x8e, 0x2f, 0x46, 0x7c,
    0x31, 0xe1, 0x8b, 0x19, 0x5f, 0xbc, 0xc6, 0x17, 0x6f, 0xf0, 0xc5, 0x5b, 0x7c, 0xf1, 0x8e, 0xff,
    0xc2, 0x3b, 0xa0, 0xe1, 0xd5, 0x04, 0x9e, 0x4d, 0xe0, 0xdd, 0x04, 0x1e, 0x4e, 0xe0, 0xe5, 0x04,
    0x9e, 0x4e, 0xe0, 0xed, 0x04, 0x1e, 0x4f, 0xe0, 0xf5, 0x38, 0xaf, 0xc7, 0x3b, 0x9c, 0x39, 0xbc,
    0x1e, 0xe7, 0xf5, 0x38, 0xaf, 0xc7, 0x79, 0x3d, 0xce, 0xeb, 0x71, 0x5e, 0x8f, 0xf3, 0x7a, 0x9c,
    0xd7, 0x13, 0x79, 0x3d, 0x91, 0xd7, 0x13, 0x3b, 0xfc, 0xb2, 0xf1, 0x7a, 0x22, 0xaf, 0x27, 0xf2,
    0x7a, 0x22, 0xaf, 0x27, 0xf2, 0x7a, 0x22, 0xaf, 0x27, 0xf2, 0x7a, 0x12, 0xaf, 0x27, 0xf1, 0x7a,
    0x12, 0xaf, 0x27, 0x75, 0xb8, 0xf1, 0xf0, 0x7a, 0x12, 0xaf, 0x27, 0xf1, 0x7a, 0x12, 0xaf, 0x27,
    0xf1, 0x7a, 0x12, 0xaf, 0x27, 0xf3, 0x7a, 0x32, 0xaf, 0x27, 0xf3, 0x7a, 0x32, 0xaf, 0x27, 0x77,
    0x08, 0x06, 0xbc, 0x9e, 0xcc, 0xeb, 0xc9, 0xbc, 0x9e, 0xcc, 0xeb, 0xc9, 0xbc, 0x1e, 0x95, 0x36,
    0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36,
    0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36,
    0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36,
    0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36,
    0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0x36, 0x95, 0xb6, 0xff, 0x2e, 0x6d, 0xdf,
};

// The same text the fixture was deflated from. Built rather than embedded: 14910 bytes of
// literal would bury the test, and a mismatch here fails loudly anyway.
std::string expectedPlain() {
  std::string one;
  for (int i = 0; i < 60; i++) {
    one += "Chapter " + std::to_string(i) +
           ": the quick brown fox jumps over the lazy dog, repeatedly and at length. ";
  }
  return one + one + one;
}

// Feeds the deflate stream in fixed-size bites, so a test can make the decompressor refill
// often (1 byte at a time) or rarely (the whole stream at once).
struct ChunkedSource {
  const uint8_t* data;
  size_t remaining;
  size_t bite;
};

size_t pullChunked(void* ctx, uint8_t* dst, size_t cap) {
  auto* s = static_cast<ChunkedSource*>(ctx);
  size_t give = s->remaining < cap ? s->remaining : cap;
  if (s->bite && give > s->bite) give = s->bite;
  if (give == 0) return 0;
  memcpy(dst, s->data, give);
  s->data += give;
  s->remaining -= give;
  return give;
}

// Runs one provider end to end. Returns the decompressed bytes; sets *ok to whether the stream
// ended cleanly.
std::string runProvider(Inflate& inflate, const size_t sourceBite, const size_t outCap, bool* ok) {
  ChunkedSource src{kDeflateStream, sizeof(kDeflateStream), sourceBite};
  std::string out;
  *ok = false;
  if (!inflate.begin(0, pullChunked, &src)) return out;

  std::vector<uint8_t> buffer(outCap);
  for (;;) {
    size_t produced = 0;
    const Inflate::Status status = inflate.read(buffer.data(), buffer.size(), &produced);
    out.append(reinterpret_cast<const char*>(buffer.data()), produced);
    if (status == Inflate::Status::StreamEnd) {
      *ok = true;
      break;
    }
    if (status == Inflate::Status::Error) break;
    if (produced == 0 && src.remaining == 0) break;  // no progress and nothing left to give
    if (out.size() > 1u << 20) break;                // runaway guard, never reached when correct
  }
  inflate.end();
  return out;
}

}  // namespace

TEST(ContentInflateTest, UzlibDecompressesTheStream) {
  UzlibContentInflate inflate;
  bool ok = false;
  const std::string out = runProvider(inflate, 0, 4096, &ok);
  EXPECT_TRUE(ok) << "stream did not end cleanly";
  EXPECT_EQ(expectedPlain(), out);
}

TEST(ContentInflateTest, MinizDecompressesTheStream) {
  freeink::content::MinizInflate inflate;
  bool ok = false;
  const std::string out = runProvider(inflate, 0, 4096, &ok);
  EXPECT_TRUE(ok) << "stream did not end cleanly";
  EXPECT_EQ(expectedPlain(), out);
}

// The point of the seam: the library must not be able to tell which provider it got.
TEST(ContentInflateTest, ProvidersAgreeAcrossChunkings) {
  const std::string expected = expectedPlain();

  // Source bites and output caps chosen to straddle the interesting cases: one byte at a time
  // (a refill per byte), smaller than the output buffer, and larger than the whole stream.
  const size_t bites[] = {1, 7, 64, 333, 4096};
  const size_t caps[] = {1, 13, 512, 4096, 32768};

  for (const size_t bite : bites) {
    for (const size_t cap : caps) {
      UzlibContentInflate uzlib;
      freeink::content::MinizInflate miniz;
      bool uzlibOk = false;
      bool minizOk = false;

      const std::string fromUzlib = runProvider(uzlib, bite, cap, &uzlibOk);
      const std::string fromMiniz = runProvider(miniz, bite, cap, &minizOk);

      EXPECT_TRUE(uzlibOk) << "uzlib: bite=" << bite << " cap=" << cap;
      EXPECT_TRUE(minizOk) << "miniz: bite=" << bite << " cap=" << cap;
      EXPECT_EQ(expected, fromUzlib) << "uzlib: bite=" << bite << " cap=" << cap;
      EXPECT_EQ(fromUzlib, fromMiniz) << "providers diverged: bite=" << bite << " cap=" << cap;
    }
  }
}

// A truncated stream must fail, not quietly return what it managed. Silent truncation on a
// protected book would render a chapter that simply stops, with nothing in the log.
TEST(ContentInflateTest, TruncatedStreamIsAnError) {
  struct ShortSource {
    const uint8_t* data;
    size_t remaining;
  };
  auto pullShort = [](void* ctx, uint8_t* dst, size_t cap) -> size_t {
    auto* s = static_cast<ShortSource*>(ctx);
    const size_t give = s->remaining < cap ? s->remaining : cap;
    if (give == 0) return 0;
    memcpy(dst, s->data, give);
    s->data += give;
    s->remaining -= give;
    return give;
  };

  const size_t half = sizeof(kDeflateStream) / 2;
  const std::string expected = expectedPlain();

  {
    ShortSource src{kDeflateStream, half};
    UzlibContentInflate inflate;
    ASSERT_TRUE(inflate.begin(0, pullShort, &src));
    std::vector<uint8_t> buffer(4096);
    std::string out;
    bool ended = false;
    for (int guard = 0; guard < 1000; guard++) {
      size_t produced = 0;
      const Inflate::Status status = inflate.read(buffer.data(), buffer.size(), &produced);
      out.append(reinterpret_cast<const char*>(buffer.data()), produced);
      if (status == Inflate::Status::StreamEnd) {
        ended = true;
        break;
      }
      if (status == Inflate::Status::Error) break;
      if (produced == 0) break;
    }
    inflate.end();
    EXPECT_FALSE(ended) << "a half stream must not report a clean end";
    EXPECT_NE(expected, out);
  }
}
