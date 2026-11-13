#pragma once

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "personalization/ReaderBars.h"

class ReaderBarsSettingsActivity final : public UiListActivity {
 public:
  ReaderBarsSettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("ReaderBarsSettings", renderer, input) {}
  void render(RenderLock&& lock) override;

 private:
  enum class View { Root, Top, Bottom };
  View view = View::Root;
  OptionPopup popup;
  freeink::ui::ListItem rows[14]{};
  int listCount() const override { return view == View::Root ? 3 : 14; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
  ReaderBar& bar();
};
