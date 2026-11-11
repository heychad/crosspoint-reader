#pragma once
#include <algorithm>

struct ReaderPageLayout {
  int top, right, bottom, left, width, height;
};

inline ReaderPageLayout makeReaderPageLayout(int width, int height, int top, int right, int bottom, int left,
                                             int horizontalMargin, int verticalMargin, int topBar, int bottomBar) {
  top += std::max(verticalMargin, topBar);
  bottom += std::max(verticalMargin, bottomBar);
  left += horizontalMargin;
  right += horizontalMargin;
  return {top, right, bottom, left, std::max(1, width - left - right), std::max(1, height - top - bottom)};
}

// Keep existing single-margin callers and saved layouts unchanged during the port.
inline ReaderPageLayout makeReaderPageLayout(int width, int height, int top, int right, int bottom, int left,
                                             int margin, int topBar, int bottomBar) {
  return makeReaderPageLayout(width, height, top, right, bottom, left, margin, margin, topBar, bottomBar);
}
