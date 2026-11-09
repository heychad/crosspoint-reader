#include <cassert>
#include <cstdint>

#include "activities/reader/ReadingChartModel.h"
namespace fui {
using ActionId = int;
struct ActionEvent {
  int action = 0, value = 0;
};
}  // namespace fui
int locks = 0;
struct RenderLock {
  template <class T>
  explicit RenderLock(T&) {
    assert(locks == 0);
    ++locks;
  }
  ~RenderLock() { --locks; }
};
struct Input {
  enum class Button { Back, Confirm, NavNext, NavPrevious };
  int released = -1;
  bool wasReleased(Button button) const { return released == static_cast<int>(button); }
};
using MappedInputManager = Input;
struct ReadingChartsActivity {
  enum class View : uint8_t { TimeOfDay, Weekdays, Calendar };
  static constexpr int ACTION_TAB = 1, ACTION_PREVIOUS = 2, ACTION_NEXT = 3, ACTION_BACK = 4;
  readingcharts::Data data;
  readingcharts::MonthRange months{300, 324};
  int month = 324;
  View view = View::TimeOfDay;
  Input mappedInput;
  struct App {
    void clearTapFlash() { assert(locks == 1); }
  } app;
  bool ready = true, finished = false, updated = false;
  int viewCount() const { return data.scope == readingcharts::Scope::Book ? 2 : 3; }
  void closeRouting() {
    assert(locks == 1);
    ready = false;
  }
  bool routingReady() const { return ready; }
  struct Touch {
    bool event = false;
  };
  Touch routeTouch(Input&) { return {}; }
  void requestUpdate() {
    assert(locks == 1);
    updated = true;
  }
  void finish() { finished = true; }
  void changeView(int);
  void step(int);
  static void onAction(const fui::ActionEvent&, void*);
  bool handleHomeGesture();
  void loop();
};
// METHODS
int main() {
  ReadingChartsActivity chart;
  chart.changeView(1);
  assert(chart.view == ReadingChartsActivity::View::Weekdays && !chart.ready);
  chart.changeView(2);
  assert(chart.view == ReadingChartsActivity::View::Weekdays);
  chart.ready = true;
  chart.mappedInput.released = int(Input::Button::Confirm);
  chart.loop();
  assert(chart.view == ReadingChartsActivity::View::TimeOfDay);
  chart.data.scope = readingcharts::Scope::Device;
  chart.changeView(2);
  assert(chart.view == ReadingChartsActivity::View::Calendar);
  chart.ready = true;
  chart.step(1);
  assert(chart.month == 324 && chart.ready);
  chart.step(-1);
  assert(chart.month == 323 && !chart.ready);
  chart.month = 300;
  chart.ready = true;
  chart.step(-1);
  assert(chart.month == 300 && chart.ready);
  chart.months = {};
  chart.step(1);
  assert(chart.month == 300);
  chart.ready = false;
  chart.mappedInput.released = int(Input::Button::Confirm);
  chart.loop();
  assert(chart.view == ReadingChartsActivity::View::Calendar);
  chart.mappedInput.released = int(Input::Button::Back);
  chart.loop();
  assert(chart.finished);
  ReadingChartsActivity touch;
  touch.data.scope = readingcharts::Scope::AllDevices;
  ReadingChartsActivity::onAction({1, 2}, &touch);
  assert(touch.view == ReadingChartsActivity::View::Calendar);
  ReadingChartsActivity::onAction({2, 0}, &touch);
  assert(touch.month == 323);
  ReadingChartsActivity::onAction({3, 0}, &touch);
  assert(touch.month == 324);
  ReadingChartsActivity::onAction({4, 0}, &touch);
  assert(touch.finished);
  ReadingChartsActivity home;
  assert(home.handleHomeGesture() && home.finished);
  assert(locks == 0);
}
