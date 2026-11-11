#pragma once
struct TestActivityManager {
  bool main = true;
  bool isMainTask() const { return main; }
};
inline TestActivityManager activityManager;
