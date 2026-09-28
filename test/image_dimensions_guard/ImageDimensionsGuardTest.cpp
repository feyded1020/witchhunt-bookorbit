// The source-size guard every framebuffer image decoder checks before allocating
// (ImageToFramebufferDecoder.cpp). width * height is computed in 64 bits, so a
// 65535 x 65535 header must be rejected rather than wrap to a small area.

#include <gtest/gtest.h>

#include "lib/Epub/Epub/converters/ImageDimensionsGuard.h"

TEST(ImageDimensionsGuard, RejectsOverflowingProduct) {
  EXPECT_TRUE(imageDimensionsWouldOverflow(65535, 65535, 3145728));
}

TEST(ImageDimensionsGuard, AcceptsReasonableSource) { EXPECT_FALSE(imageDimensionsWouldOverflow(2048, 1536, 3145728)); }

TEST(ImageDimensionsGuard, RejectsNonPositiveDimensions) {
  EXPECT_TRUE(imageDimensionsWouldOverflow(0, 100, 3145728));
  EXPECT_TRUE(imageDimensionsWouldOverflow(100, -1, 3145728));
}
