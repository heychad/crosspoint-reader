#pragma once

#include "ReaderBars.h"
class GfxRenderer;

namespace readerbars {
int height(const ReaderBar& bar, bool forceText = false);
void draw(GfxRenderer& renderer, const ReaderBar& bar, ReaderBarPosition position, float bookPercent, int page,
          int pages, bool estimated, const char* bookTitle, const char* chapterTitle, bool bookmarked,
          const char* overrideTitle = nullptr, const char* bookTime = nullptr, const char* sectionTime = nullptr,
          const char* chapterTime = nullptr);
}  // namespace readerbars
