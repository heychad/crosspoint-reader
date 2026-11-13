#pragma once
#include <functional>
#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ConfirmationActivity : public Activity {
 private:
  // Input data
  std::string heading;
  std::string body;
  std::string cancelLabel;
  std::string confirmLabel;
  bool confirmOnly;

  OptionPopup confirmPopup;

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body, const std::string& cancelLabel = {},
                       const std::string& confirmLabel = {}, bool confirmOnly = false);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
