#include "TextSettingsPreview.h"

#include <EpdFontFamily.h>
#include <Epub/ParsedText.h>
#include <Epub/blocks/BlockStyle.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace textsettings {

PreviewLayout::PreviewLayout() = default;
PreviewLayout::~PreviewLayout() = default;

namespace {

// Map the paragraph-alignment setting to the engine's CssTextAlign (BOOK_STYLE = justified)
CssTextAlign toCssAlign(uint8_t align) {
  if (align == CrossPointSettings::BOOK_STYLE) return CssTextAlign::Justify;
  return static_cast<CssTextAlign>(align);
}

uint32_t textHash(const char* text) {
  uint32_t hash = 2166136261U;
  for (; *text; ++text) hash = (hash ^ static_cast<uint8_t>(*text)) * 16777619U;
  return hash;
}

// Keep a bounded set of lines; the source excerpt is bounded by its caller.
void relayout(PreviewLayout& layout, const GfxRenderer& renderer, int fontId, int textWidth, const char* text) {
  layout.lines.clear();
  layout.lines.reserve(MAX_PREVIEW_LINES);
  BlockStyle style;
  style.alignment = toCssAlign(SETTINGS.paragraphAlignment);
  style.textAlignDefined = true;
  const char* paragraph = text;
  while (*paragraph && layout.lines.size() < MAX_PREVIEW_LINES) {
    ParsedText parsed(SETTINGS.hyphenationEnabled != 0, SETTINGS.focusReadingEnabled != 0, style,
                      SETTINGS.paragraphIndentSpaces);
    const char* end = paragraph;
    while (*end && *end != '\n') ++end;
    std::string word;
    for (const char* p = paragraph;; ++p) {
      if (p == end || *p == ' ') {
        if (!word.empty()) {
          parsed.addWord(word, EpdFontFamily::REGULAR);
          word.clear();
        }
        if (p == end) break;
      } else {
        word.push_back(*p);
      }
    }
    parsed.layoutAndExtractLines(
        renderer, fontId, static_cast<uint16_t>(textWidth),
        [&layout](std::unique_ptr<TextBlock> line, uint32_t) {
          if (layout.lines.size() < MAX_PREVIEW_LINES) layout.lines.push_back(std::move(line));
        },
        true, SETTINGS.getCharacterSpacing(), SETTINGS.wordSpacing);
    if (*end && layout.lines.size() < MAX_PREVIEW_LINES) layout.lines.push_back(nullptr);
    paragraph = *end ? end + 1 : end;
  }
}

}  // namespace

void renderPreview(const GfxRenderer& renderer, PreviewLayout& layout, int previewPadding, int labelGap, Rect bounds,
                   const char* caption, const char* sourceText) {
  const int top = bounds.y;
  const int height = bounds.height;
  const int left = bounds.x + previewPadding;
  const int width = bounds.width - (previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelH = renderer.getTextHeight(UI_10_FONT_ID);
  const int labelReserved = labelH + labelGap + previewPadding;

  const int labelY = top + height - previewPadding - labelH;
  renderer.drawText(UI_10_FONT_ID, left, labelY, caption);

  const int fontId = SETTINGS.getReaderFontId();
  if (fontId == 0) return;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int textLeft = left + SETTINGS.screenMargin;
  const int textWidth = width - 2 * SETTINGS.screenMargin;
  if (textWidth <= 0) return;

  const float compression = SETTINGS.getReaderLineCompression();
  const int lineAdvance = std::max(1, renderer.getLineHeight(fontId, compression));
  const int paragraphGap = SETTINGS.extraParagraphSpacing ? lineAdvance / 2 : 0;

  // Re-lay-out (and re-prewarm glyphs) only when a layout-affecting setting or the
  // geometry changed; else reuse the cache. The prewarm inputs are (fontId, constant
  // sample text, styleMask<-focusReading), all of which are key fields, so a matching
  // key means an identical prewarm call. This relies on nothing else evicting the SD
  // glyph cache while this activity is up — true today: the only evictor is
  // FontCacheManager::PrewarmScope, used solely by the reader/dictionary activities.
  const char* text = sourceText && *sourceText ? sourceText : I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  const PreviewKey key{.textHash = textHash(text),
                       .fontHash = textHash(SETTINGS.sdFontFamilyName),
                       .fontId = fontId,
                       .fontPointSize = SETTINGS.fontPointSize,
                       .screenMargin = SETTINGS.screenMargin,
                       .textWidth = textWidth,
                       .lineCompression = compression,
                       .alignment = SETTINGS.paragraphAlignment,
                       .extraParagraphSpacing = SETTINGS.extraParagraphSpacing != 0,
                       .paragraphIndentSpaces = SETTINGS.paragraphIndentSpaces,
                       .characterSpacing = SETTINGS.getCharacterSpacing(),
                       .wordSpacingPercent = SETTINGS.wordSpacing,
                       .focusReading = SETTINGS.focusReadingEnabled != 0,
                       .hyphenation = SETTINGS.hyphenationEnabled != 0};
  if (key != layout.key) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->prewarmCache(fontId, text, SETTINGS.focusReadingEnabled ? 0x03 : 0x01);
    }
    relayout(layout, renderer, fontId, textWidth, text);
    layout.key = key;
  }

  // Repeat only the fallback sample; book excerpts keep their actual paragraph breaks.
  int y = top + previewPadding;
  const int textBottomLimit = top + height - labelReserved;
  const int repeats = sourceText && *sourceText ? 1 : 2;
  for (int paragraph = 0; paragraph < repeats; paragraph++) {
    for (const auto& line : layout.lines) {
      if (!line) {
        y += paragraphGap;
        continue;
      }
      if (y + lineH > textBottomLimit) return;
      line->render(renderer, fontId, textLeft, y);
      y += lineAdvance;
    }
    y += paragraphGap;
  }
}

}  // namespace textsettings
