#pragma once

#include "activities/Activity.h"
#include "activities/reader/ReadingChartModel.h"
#include "components/UiAppHost.h"

class ReadingChartsActivity final : public Activity, protected UiAppHost {
 public:
  ReadingChartsActivity(GfxRenderer&, MappedInputManager&, const readingcharts::Data&, const char* title);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool acceptsRemoteInput() const override { return routingReady(); }

 private:
  enum class View : uint8_t { TimeOfDay, Weekdays, Calendar };
  static constexpr freeink::ui::ActionId ACTION_TAB = 1;
  static constexpr freeink::ui::ActionId ACTION_PREVIOUS = 2;
  static constexpr freeink::ui::ActionId ACTION_NEXT = 3;
  static constexpr freeink::ui::ActionId ACTION_BACK = 4;
  static void buildScreen(UiScreen&, void*);
  static void onAction(const freeink::ui::ActionEvent&, void*);
  void build(UiScreen&);
  void drawBars(UiScreen&, freeink::ui::Rect);
  void drawCalendar(UiScreen&, freeink::ui::Rect);
  void changeView(int view);
  void step(int direction);
  int viewCount() const { return data.scope == readingcharts::Scope::Book ? 2 : 3; }

  const readingcharts::Data data;
  const readingcharts::MonthRange months;
  View view = View::TimeOfDay;
  int month = -1;
  std::array<freeink::ui::TabItem, 3> tabs{};
  freeink::ui::TabBarProps tabProps;
  freeink::ui::ButtonProps button;
  char title[160]{};
  char text[80]{};
};
