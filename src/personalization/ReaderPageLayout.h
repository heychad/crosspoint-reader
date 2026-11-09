#pragma once
#include <algorithm>

struct ReaderPageLayout {
  int top, right, bottom, left, width, height;
};

inline ReaderPageLayout makeReaderPageLayout(int width, int height, int top, int right, int bottom, int left,
                                             int margin, int topBar, int bottomBar) {
  top += std::max(margin, topBar);
  bottom += std::max(margin, bottomBar);
  left += margin;
  right += margin;
  return {top, right, bottom, left, std::max(1, width - left - right), std::max(1, height - top - bottom)};
}
