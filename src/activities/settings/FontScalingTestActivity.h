#pragma once

#include "activities/Activity.h"

/// Settings > System > Font Scaling Test.
///
/// Draws a real pre-rendered face beside the same size produced by SCALING a different face, so
/// the two can be compared by eye on the panel at its true resolution.
///
/// This exists because the measurements cannot answer the question. bench/font_main.cpp puts the
/// mean absolute coverage error of an area-weighted resample at 6-8% over the ratios the size
/// ladder would use, and that number is genuinely reassuring — but MAD is an average over
/// coverage, and what a reader notices is edge definition and stem weight. Those are exactly what
/// area weighting trades away: it reconstructs a grey edge where the real face has a crisp one.
/// A page that reads 6.6% might be indistinguishable or might look soft, and nothing in the
/// benchmark distinguishes those two outcomes.
///
/// So the decision this screen serves is whether the built-in ladder can ship fewer real faces
/// and synthesise the rest — worth roughly 0.6 to 1.4 MB of the app partition depending on how
/// many masters survive.
///
/// Deliberately goes through drawTextScaled() and the normal grayscale passes rather than
/// resampling anything itself. The benchmark's resampler MIRRORS production; this screen must BE
/// production, including the anti-aliasing planes, or it would answer about the wrong code.
class FontScalingTestActivity final : public Activity {
 public:
  explicit FontScalingTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FontScalingTest", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  /// One comparison: `size` pt drawn for real, against `size` pt scaled from `fromSize`.
  struct Page {
    const char* title;
    int realFontId;
    int masterFontId;
    float scale;  ///< masterFontId is drawn at this scale to reach the real face's size
    bool runningText;
  };

  void renderContent() const;

  uint8_t page_ = 0;
  static const Page kPages[];
  static const uint8_t kPageCount;
};
