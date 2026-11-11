#pragma once
struct TestActivityManager {
  bool isMainTask() const { return true; }
};
inline TestActivityManager activityManager;
