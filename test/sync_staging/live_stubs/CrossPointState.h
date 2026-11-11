#pragma once
#include <string>
struct FakeState {
  std::string openEpubPath;
};
inline FakeState fakeState;
#define APP_STATE fakeState
