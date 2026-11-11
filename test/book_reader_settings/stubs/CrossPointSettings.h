#pragma once
#include <cstdint>

#include "Epub/EpubRenderMode.h"
class CrossPointSettings {
 public:
  uint8_t fontPointSize = 14;
  uint8_t lineSpacing = 2;
  uint8_t wordSpacing = 100;
  uint8_t characterSpacing = 2;
  uint8_t orientation = 0;
  uint8_t screenMargin = 10;
  uint8_t paragraphAlignment = 0;
  uint8_t extraParagraphSpacing = 1;
  uint8_t paragraphIndentSpaces = 2;
  uint8_t hyphenationEnabled = 1;
  uint8_t embeddedStyle = 1;
  uint8_t textAntiAliasing = 1;
  uint8_t imageRendering = 0;
  uint8_t epubRenderMode = 0;
  enum INDEXING_METHOD { INDEXING_INCREMENTAL = 0, INDEXING_FULL_SECTION, INDEXING_METHOD_COUNT };
  uint8_t indexingMethod = INDEXING_INCREMENTAL;
  uint8_t focusReadingEnabled = 0;
  uint8_t guideReadingEnabled = 0;
  char sdFontFamilyName[32]{};
  char dictionaryName[32]{};
};
