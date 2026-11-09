#pragma once

#include <array>

#include "activities/UiListActivity.h"
#include "personalization/ReaderQuickActions.h"
#include "personalization/ReaderShortcutLabels.h"

class QuickActionsActivity final : public UiListActivity {
 public:
  enum class Mode { Run, Configure, Choose };
  QuickActionsActivity(GfxRenderer& renderer, MappedInputManager& input, Mode mode, uint64_t allowedActions);
  void onEnter() override;

 private:
  Mode mode;
  bool returnToRun = false;
  uint64_t allowedActions;
  std::array<readershortcuts::Action, readerquickactions::SLOT_COUNT> visibleActions{};
  int visibleCount = 0;
  inline static constexpr std::array<StrId, readerquickactions::SLOT_COUNT> SLOT_LABELS = {
      StrId::STR_QUICK_ACTION_1, StrId::STR_QUICK_ACTION_2, StrId::STR_QUICK_ACTION_3, StrId::STR_QUICK_ACTION_4,
      StrId::STR_QUICK_ACTION_5};
  std::array<readershortcuts::Action, static_cast<size_t>(readershortcuts::Action::Count)> optionActions{};
  int optionCount = 0;
  int selectedSlot = 0;
  void reload();
  void changeMode(Mode next, int selection = 0);
  void openSlot(int slot);
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& item);
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
};

static_assert(sizeof(QuickActionsActivity) <= sizeof(UiListActivity) + 384, "Review Quick Actions state growth");
