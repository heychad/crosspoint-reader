#pragma once

// Layout helpers adapted from CrossInk's ReaderStatusBarConfig.h (MIT).
// Copyright (c) 2025 Dave Allie and CrossInk contributors.
#include <algorithm>
#include <array>
#include <cstdint>

enum class ReaderBarPosition : uint8_t { Top, Bottom };
enum class ReaderBarItem : uint8_t {
  Empty,
  Clock,
  Battery,
  ChapterPages,
  BookPercent,
  BookTitle,
  ChapterTitle,
  Date,
  TimeLeftBook,
  TimeLeftSection,
  Count
};

struct ReaderBar {
  static constexpr unsigned SLOT_COUNT = 7;
  static constexpr unsigned CENTER = 3;
  std::array<ReaderBarItem, SLOT_COUNT> slots{};
  uint8_t progress = 2;  // CrossPoint values: book, chapter, hidden.
  uint8_t thickness = 1;
  uint8_t percentageDecimals = 0;

  bool hasText(bool clockAvailable = true) const {
    for (auto item : slots) {
      if (item != ReaderBarItem::Empty &&
          ((item != ReaderBarItem::Clock && item != ReaderBarItem::Date) || clockAvailable))
        return true;
    }
    return false;
  }
  bool contains(ReaderBarItem wanted) const { return std::find(slots.begin(), slots.end(), wanted) != slots.end(); }
  int height(int textHeight, int progressGap) const {
    return (hasText() ? textHeight : 0) + (progress < 2 ? (thickness + 1) * 2 + progressGap : 0);
  }
};

struct ReaderBarLayout {
  std::array<int, ReaderBar::SLOT_COUNT> x{};
  int centerLeft = 0;
  int centerRight = 0;
};

inline ReaderBarLayout layoutReaderBar(std::array<int, ReaderBar::SLOT_COUNT>& widths, int left, int right,
                                       int reserve = 0, int gap = 8) {
  right = std::max(left, right);
  reserve = std::clamp(reserve, 0, right - left);
  int leftCount = 0, rightCount = 0;
  for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i) {
    if (i < ReaderBar::CENTER && widths[i] > 0) ++leftCount;
    if (i > ReaderBar::CENTER && widths[i] > 0) ++rightCount;
  }
  const int gaps = std::max(0, leftCount - 1) + std::max(0, rightCount - 1) + (reserve && leftCount ? 1 : 0);
  const int budget = std::max(0, right - left - reserve - gaps * gap);
  int low = 0, high = budget;
  while (low < high) {
    int cap = low + (high - low + 1) / 2, used = 0;
    for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i) {
      if (i != ReaderBar::CENTER) used += std::min(widths[i], cap);
    }
    if (used <= budget)
      low = cap;
    else
      high = cap - 1;
  }
  for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i) {
    if (i != ReaderBar::CENTER) widths[i] = std::min(widths[i], low);
  }
  ReaderBarLayout result;
  int x = left + reserve;
  bool occupied = reserve > 0;
  for (unsigned i = 0; i < ReaderBar::CENTER; ++i) {
    if (!widths[i]) continue;
    if (occupied) x += gap;
    result.x[i] = x;
    x += widths[i];
    occupied = true;
  }
  int rightWidth = 0;
  for (unsigned i = ReaderBar::CENTER + 1; i < ReaderBar::SLOT_COUNT; ++i) {
    if (!widths[i]) continue;
    if (rightWidth) rightWidth += gap;
    rightWidth += widths[i];
  }
  int rx = right - rightWidth;
  for (unsigned i = ReaderBar::CENTER + 1; i < ReaderBar::SLOT_COUNT; ++i) {
    result.x[i] = rx;
    if (widths[i]) rx += widths[i] + gap;
  }
  result.centerLeft = std::clamp(x + (occupied ? gap : 0), left, right);
  result.centerRight = std::clamp(right - rightWidth - (rightWidth ? gap : 0), result.centerLeft, right);
  return result;
}

inline ReaderBarPosition transientReaderBar(const ReaderBar& top, const ReaderBar& bottom, bool autoTurn) {
  const auto hasTitle = [](const ReaderBar& bar) {
    return bar.contains(ReaderBarItem::BookTitle) || bar.contains(ReaderBarItem::ChapterTitle);
  };
  return !autoTurn && hasTitle(top) && !hasTitle(bottom) ? ReaderBarPosition::Top : ReaderBarPosition::Bottom;
}
