#pragma once
#include <string>
struct TestCrossPointState {
  std::string openEpubPath;
  bool updateOpenEpubPathPersisted(const std::string& from, const std::string& to) {
    if (openEpubPath == from) openEpubPath = to;
    return true;
  }
};
inline TestCrossPointState testAppState;
#define APP_STATE testAppState
