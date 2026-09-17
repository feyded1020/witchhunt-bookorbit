#pragma once
#include <string>

#include "../Activity.h"
#include "components/UiAppHost.h"

// Yes/No prompt. Built on fui::optionDialog rather than hand-drawn text, so the two answers are
// on-screen buttons a finger can hit.
//
// That is the point of the change: this screen used to draw a heading, a body line and nothing
// else, leaving the button-hint strip as its ONLY touch affordance. On a touch board there was no
// Cancel or Confirm target anywhere on the prompt itself.
//
// The hint strip is still drawn, because it is how the physical buttons are labelled and it is the
// only affordance a non-touch board has. The two are alternatives, not duplicates.
//
// The constructor and the ActivityResult contract are unchanged, so the six callers are untouched.
class ConfirmationActivity : public Activity, private UiAppHost {
 private:
  std::string heading;
  std::string body;
  bool inputArmed = false;

  static void dialogScreen(UiScreen& screen, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onConfirmEvent(const freeink::ui::ActionEvent& event, void* user);

  void buildDialogScreen(UiScreen& screen);
  void finishWith(bool cancelled);

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
