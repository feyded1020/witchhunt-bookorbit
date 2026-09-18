#pragma once

#include "../Activity.h"
#include "bookorbit/HighlightStore.h"
#include "util/ButtonNavigator.h"

// The current book's highlights: Confirm jumps to one, logical Right deletes it (the deletion
// reaches BookOrbit on the next sync). Edits go straight to the reader's store and are saved
// when this screen closes.
class HighlightsActivity final : public Activity {
  HighlightStore& store;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  std::string getItemLabel(int index) const;
  void deleteSelected();
  void close();

 public:
  explicit HighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, HighlightStore& store)
      : Activity("Highlights", renderer, mappedInput), store(store) {}
  void onEnter() override;
  void onExit() override;
  ListRowTap::Result selectListRow(int index) override;
  void loop() override;
  void render(RenderLock&&) override;
};
