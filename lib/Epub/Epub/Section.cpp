#include "Section.h"

#include <BufferedFile.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <algorithm>

#include "Epub/css/CssParser.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v28: text decoration bits now include line-through in serialized wordStyles.
// v29: TextBlock word data stored as one flat arena (offset table + NUL-terminated
// text blob) instead of length-prefixed strings and per-field arrays.
// v30: Arabic shaping changed both drawing and measurement (getTextAdvanceX now
//      measures the shaped visual text); cached word positions from v29 no longer
//      match what drawText renders.
// v32: ImageBlock serializes the book-internal source href after the cache path
//      (lazy extraction: images are header-probed at build time and extracted on
//      first render).
// v33: Support <ruby> and <rt> tags. Skip <rp> tags
// v34: Word gaps are only suppressed for tokens glued in the source, so spaces between
//      Hangul words survive again; ruby element boundaries carry the continuation flag
//      instead. Invalidates v33 caches, whose word positions have the spaces collapsed.

// v34: <br> handling changed layout — a <br> after text is now a margin-stripped
//      line break (browser-like) and only a <br> whose block stays empty injects
//      the scene-break gap, so cached pages laid out by older versions no longer
//      match. Keeps <br>-per-paragraph books (common CJK formatting) from
//      re-adding container spacing at every paragraph.
// v35: Persist a uint32_t visible-text start offset for every page.
// v36: Ruby and CJK justification layout changes invalidate cached word positions.
// v37: Footnote href records grew from 96 to 256 bytes.
// v38: Focus Reading line breaking changed — a visible hyphen/dash inside a word is now a
//      break opportunity, and hyphenation of a focus-split word considers the whole word
//      instead of only its regular-weight suffix. Pages cached by older versions were laid
//      out with the previous, more restrictive break set and no longer match.
// v39: Image top margin is clamped so a full-viewport-height image cannot
//      overflow the page bottom; older caches can hold placements that panels
//      with no bottom inset refuse to draw.
// v40: Ruby groups remain intact when a large text block is soft-flushed.
// v41: Simple HTML table rows are laid out as positioned columns instead of
//      flattened paragraphs with synthetic row/cell labels.
// v42: Closing a block strips inherited vertical margins and padding.
// v43: Paragraph base direction excludes direction changes from inline elements.
// v44: Persist internal-link rectangles with each page for touch navigation.
// v45: Internal EPUB links preserve CSS superscript/subscript positioning.
// v46: Ordered lists number their items, list-style-type: none suppresses markers,
//      and <ul>/<ol> containers contribute their own margins/padding to child insets.
// v47: Word and character spacing in the header (cache validation); cached BlockStyle stores only character spacing.
// v48: Hangul words wrap at spaces; with hyphenation on they may also split at a line end.
//      Justification no longer stretches between syllables.
// v49 was used by pre-release builds with a different header layout.
// v50: Paragraph indentation width in the header for cache validation.
// v51: Preserve paragraph continuity and top spacing across soft flushes.
// v52: Missing full-block and black-square symbols now have visible widths.
// v53: Persist per-word source ranges alongside the redaction layout changes.
// v54: Source ranges retain codepoints absorbed by NFC composition.
// v55: Persist the first logical word of each paragraph for clipping separators.
// v56: Rendering layout (fonts per word, drop caps, border boxes).
// v57: Includes explicit hyphen/dash line breaks and invalidates both v56 layouts.
// v58: Rebuild sections for corrected rendering layout and footnote placement.
// v59: Preserve stored CSS styles during low-heap layout.
// v60: Text running into a block or heading start stays in its own paragraph.
// Earlier GIF/table development builds also used versions 60 through 62.
// v63: Rebuild for corrected paragraph boundaries and supported static GIF images.
// v64: Explicit font inheritance preserves parent emphasis and changes text layout.
// v65: Count literal spaces after hyphens and ellipses when justifying text.
// v66: Omit carried no-break spaces at the start of wrapped lines.
// v67: Count only stretchable literal spaces after visual text reordering.
// v68: Leave text below drop caps unsplit until it is laid out at full width.
// v69: Line spacing multiplies font size instead of advanceY.
// v70: SD-font layout rounds each scaled glyph advance to match drawing.
// v71: Focus split offsets follow the visual run order for RTL words.
// v72: Make ruby rendering closer to how Chromium renders ruby by default.
// v73: TrueType vector fonts load and advance .notdef for missing glyphs.
// v78: Rendering mode is included in the header.
constexpr uint8_t SECTION_FILE_VERSION = 78;
// Written into the version field while a build is in progress; patched to
// SECTION_FILE_VERSION only when the build is finalized. An abandoned /
// crash-interrupted .bin therefore carries version 0, which loadSectionFile rejects
// as unknown and clears -- so an incomplete file is never mistaken for a valid one.
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
// Written when a build is suspended partway (reader exited or device slept mid-build).
// The file carries valid pages 0..pageCount-1, all LUTs, and a trailer with the parse
// watermark (bytesConsumed, totalBytes) appended after the li LUT. loadSectionFile
// accepts it so a resume shows those pages instantly; the reader extends it by
// rebuilding in the background. Uses the same header layout as SECTION_FILE_VERSION,
// so finalized files are untouched by this feature; older firmware treats the sentinel
// as an unknown version and rebuilds, which is a safe downgrade.
// MUST change in lockstep with SECTION_FILE_VERSION: the sentinel IS the partial's
// format version, so a stale-format partial otherwise passes the header check and
// only fails (noisily, via the block-decode error path) when a page is loaded.
// Derived so the pairing can't be forgotten: 0xFE for v28, 0xFD for v29, ...
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = 0xFE - (SECTION_FILE_VERSION - 28);
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(bool) + sizeof(uint32_t) + sizeof(uint32_t) +
                                 sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(int8_t) +
                                 sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t);
}  // namespace

// Out-of-line so the unique_ptr<ChapterHtmlSlimParser> in BuildContext can be
// constructed/destroyed where the parser's full definition is visible.
Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}

// Suspend any in-progress build so every section.reset() / navigation / sleep path
// persists the pages already laid out as a partial .bin instead of discarding them
// (no-op once a build has completed or never started).
Section::~Section() {
  if (speculativeMaxPages_)
    abandonBuild();
  else
    suspendBuild();
}

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (speculativeMaxPages_ && builtPageCount_ >= speculativeMaxPages_) {
    if (build_ && build_->parser) build_->parser->requestStop();
    return 0;
  }
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", builtPageCount_);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", builtPageCount_);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", builtPageCount_);

  builtPageCount_++;
  // pageCount is the pages available to read: a rebuild over a partial only raises it
  // once it has laid out more pages than the partial already covers.
  if (builtPageCount_ > pageCount) {
    pageCount = builtPageCount_;
  }
  return position;
}

bool Section::writeSectionFileHeader(const ReaderRenderSpec& spec) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return false;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(spec.fontId) + sizeof(spec.lineCompression) +
                                   sizeof(spec.extraParagraphSpacing) + sizeof(spec.paragraphIndentSpaces) +
                                   sizeof(spec.paragraphAlignment) + sizeof(spec.viewportWidth) +
                                   sizeof(spec.viewportHeight) + sizeof(pageCount) + sizeof(spec.hyphenationEnabled) +
                                   sizeof(spec.embeddedStyle) + sizeof(spec.imageRendering) +
                                   sizeof(spec.focusReadingEnabled) + sizeof(spec.characterSpacing) +
                                   sizeof(spec.wordSpacingPercent) + sizeof(uint8_t) + sizeof(uint32_t) +
                                   sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t),
                "Header size mismatch");
  // Written as the incomplete sentinel; finalizeBuild() patches it to
  // SECTION_FILE_VERSION as the last step, committing the file.
  return serialization::tryWritePod(file, SECTION_FILE_INCOMPLETE_VERSION) &&
         serialization::tryWritePod(file, spec.fontId) && serialization::tryWritePod(file, spec.lineCompression) &&
         serialization::tryWritePod(file, spec.extraParagraphSpacing) &&
         serialization::tryWritePod(file, spec.paragraphIndentSpaces) &&
         serialization::tryWritePod(file, spec.paragraphAlignment) &&
         serialization::tryWritePod(file, spec.viewportWidth) &&
         serialization::tryWritePod(file, spec.viewportHeight) &&
         serialization::tryWritePod(file, spec.hyphenationEnabled) &&
         serialization::tryWritePod(file, spec.embeddedStyle) &&
         serialization::tryWritePod(file, spec.imageRendering) &&
         serialization::tryWritePod(file, spec.focusReadingEnabled) &&
         serialization::tryWritePod(file, spec.characterSpacing) &&
         serialization::tryWritePod(file, spec.wordSpacingPercent) &&
         serialization::tryWritePod(file, static_cast<uint8_t>(spec.renderMode)) &&
         serialization::tryWritePod(file, pageCount) && serialization::tryWritePod(file, uint32_t{0}) &&
         serialization::tryWritePod(file, uint32_t{0}) && serialization::tryWritePod(file, uint32_t{0}) &&
         serialization::tryWritePod(file, uint32_t{0}) && serialization::tryWritePod(file, uint32_t{0});
}

bool Section::loadSectionFile(const ReaderRenderSpec& spec) {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  bool filePartial = false;
  {
    uint8_t version = 0;
    if (!serialization::tryReadPod(file, version) ||
        (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION)) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }
    filePartial = (version == SECTION_FILE_PARTIAL_VERSION);

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphIndentSpaces;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    uint8_t fileImageRendering;
    bool fileFocusReadingEnabled;
    int8_t fileCharacterSpacing;
    uint8_t fileWordSpacingPercent;
    uint8_t fileRenderMode;
    if (!serialization::tryReadPod(file, fileFontId) || !serialization::tryReadPod(file, fileLineCompression) ||
        !serialization::tryReadPod(file, fileExtraParagraphSpacing) ||
        !serialization::tryReadPod(file, fileParagraphIndentSpaces) ||
        !serialization::tryReadPod(file, fileParagraphAlignment) ||
        !serialization::tryReadPod(file, fileViewportWidth) || !serialization::tryReadPod(file, fileViewportHeight) ||
        !serialization::tryReadPod(file, fileHyphenationEnabled) ||
        !serialization::tryReadPod(file, fileEmbeddedStyle) || !serialization::tryReadPod(file, fileImageRendering) ||
        !serialization::tryReadPod(file, fileFocusReadingEnabled) ||
        !serialization::tryReadPod(file, fileCharacterSpacing) ||
        !serialization::tryReadPod(file, fileWordSpacingPercent) || !serialization::tryReadPod(file, fileRenderMode)) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: truncated section header");
      clearCache();
      return false;
    }

    if (spec.fontId != fileFontId || spec.lineCompression != fileLineCompression ||
        spec.extraParagraphSpacing != fileExtraParagraphSpacing ||
        spec.paragraphIndentSpaces != fileParagraphIndentSpaces || spec.paragraphAlignment != fileParagraphAlignment ||
        spec.viewportWidth != fileViewportWidth || spec.viewportHeight != fileViewportHeight ||
        spec.hyphenationEnabled != fileHyphenationEnabled || spec.embeddedStyle != fileEmbeddedStyle ||
        spec.imageRendering != fileImageRendering || spec.focusReadingEnabled != fileFocusReadingEnabled ||
        spec.characterSpacing != fileCharacterSpacing || spec.wordSpacingPercent != fileWordSpacingPercent ||
        static_cast<uint8_t>(spec.renderMode) != fileRenderMode) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  if (!serialization::tryReadPod(file, pageCount)) {
    file.close();
    LOG_ERR("SCT", "Deserialization failed: missing page count");
    clearCache();
    pageCount = 0;
    return false;
  }

  if (filePartial) {
    // A partial's pageCount is the watermark of a suspended build. Read the watermark
    // trailer (appended after the visible-offset LUT) so estimatedTotalPages can extrapolate.
    uint32_t liLutOffset = 0;
    const bool offsetsRead =
        file.seek(HEADER_SIZE - sizeof(uint32_t) * 2) && serialization::tryReadPod(file, liLutOffset);
    uint32_t visibleLutOffset = 0;
    const bool visibleRead =
        file.seek(HEADER_SIZE - sizeof(uint32_t)) && serialization::tryReadPod(file, visibleLutOffset);
    const uint32_t trailerOffset = visibleLutOffset + static_cast<uint32_t>(pageCount) * sizeof(uint32_t);
    const bool trailerValid = offsetsRead && visibleRead && pageCount > 0 && liLutOffset >= HEADER_SIZE &&
                              visibleLutOffset > liLutOffset && trailerOffset + 2 * sizeof(uint32_t) <= file.size();
    if (!trailerValid) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: malformed partial section");
      clearCache();
      pageCount = 0;
      return false;
    }
    if (!file.seek(trailerOffset) || !serialization::tryReadPod(file, partialBytesConsumed_) ||
        !serialization::tryReadPod(file, partialTotalBytes_)) {
      file.close();
      clearCache();
      pageCount = 0;
      return false;
    }
    partial_ = true;
    partialPageCount_ = pageCount;
  }

  // Explicit close() required: member variable persists beyond function scope
  file.close();
  LOG_DBG("SCT", "Deserialization succeeded: %d pages%s", pageCount, filePartial ? " (partial)" : "");
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  const std::string tmpBin = binTmpPath();
  if (Storage.exists(tmpBin.c_str())) {
    Storage.remove(tmpBin.c_str());
  }
  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  // One-shot build: start, then lay out the whole section in a single pass.
  if (!startBuild(spec, popupFn)) {
    return false;
  }
  if (!buildSomeMore(0)) {  // 0 = build to completion
    return false;
  }
  return buildComplete_;
}

bool Section::openSpeculativeOutput() {
  return Storage.withStorageLock(
      [](void* context) {
        auto* section = static_cast<Section*>(context);
        const std::string tmp = section->filePath + ".part";
        const std::string speculative = section->speculativeFilePath();
        if (Storage.lookupPath(section->filePath.c_str()) != HalStorage::PathStatus::Missing ||
            Storage.lookupPath(tmp.c_str()) != HalStorage::PathStatus::Missing ||
            Storage.lookupPath(speculative.c_str()) != HalStorage::PathStatus::Missing)
          return false;
        section->file = Storage.open(speculative.c_str(), O_RDWR | O_CREAT | O_EXCL);
        return static_cast<bool>(section->file);
      },
      this);
}

bool Section::startBuild(const ReaderRenderSpec& spec, const std::function<void()>& popupFn,
                         const uint16_t speculativePages, const uint16_t speculativeAnchors) {
  if (!speculativePages && !speculativeAnchors) return startBuildInternal(spec, popupFn, 0, 0);
  struct Request {
    Section* section;
    const ReaderRenderSpec* spec;
    const std::function<void()>* popup;
    uint16_t pages;
    uint16_t anchors;
  } request{this, &spec, &popupFn, speculativePages, speculativeAnchors};
  return Storage.withStorageLock(
      [](void* context) {
        const auto& request = *static_cast<Request*>(context);
        auto& section = *request.section;
        if (!Storage.monitoredStorageHealthy() ||
            !section.startBuildInternal(*request.spec, *request.popup, request.pages, request.anchors))
          return false;
        if (section.closeSpeculativeOutput()) return true;
        section.preserveSpeculativeOutput_ = true;
        section.abandonBuild();
        return false;
      },
      &request);
}

bool Section::startBuildInternal(const ReaderRenderSpec& spec, const std::function<void()>& popupFn,
                                 const uint16_t speculativePages, const uint16_t speculativeAnchors) {
  if (build_) {
    LOG_ERR("SCT", "startBuild called while a build is already active");
    return false;
  }
  if ((speculativePages == 0) != (speculativeAnchors == 0)) return false;
  speculativeMaxPages_ = speculativePages;
  speculativeMaxAnchors_ = speculativeAnchors;
  preserveSpeculativeOutput_ = false;
  // Reclaim rebuildable font caches before CSS and layout allocations.
  if (!speculativeMaxPages_) {
    if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseSdFontCaches();
  }
  buildComplete_ = false;
  builtPageCount_ = 0;
  // Pages from a loaded partial stay readable (from filePath) while this build writes
  // to the tmp .bin, so availability never drops below the partial's watermark.
  pageCount = partial_ ? partialPageCount_ : 0;

  // Remove a stale tmp .bin from a crash-interrupted build; this build recreates it.
  if (!speculativeMaxPages_) {
    const std::string staleTmp = binTmpPath();
    if (Storage.exists(staleTmp.c_str())) {
      Storage.remove(staleTmp.c_str());
    }
  }

  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto htmlDir = epub->getCachePath() + "/html";
  const auto htmlPath = htmlDir + "/" + std::to_string(spineIndex) + ".html";
  const auto tmpHtmlPath = htmlDir + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  if (!speculativeMaxPages_) {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    if (!Storage.exists(sectionsDir.c_str())) Storage.mkdir(sectionsDir.c_str());
  }

  // Reuse the previously unzipped HTML if we already have it. The unzipped HTML is keyed only on the
  // book (it lives in the per-book cache dir), not on render settings, so it survives the invalidation
  // that wipes the layout (.bin) caches when font/margin/orientation change -- rebuilds then skip zip
  // inflation entirely. It's promoted by an atomic rename as soon as the inflate succeeds (below), so
  // even a window-only giant spine -- whose .bin never finalizes -- still caches its HTML, letting a
  // reopen skip the multi-second inflate. If htmlPath exists it is known-complete.
  const bool reusedHtml = Storage.exists(htmlPath.c_str());
  // Avoid blocking ZIP extraction until speculative streaming is cancellable.
  if (speculativeMaxPages_ && !reusedHtml) return false;
  bool htmlCached = reusedHtml;
  if (reusedHtml) {
    LOG_DBG("SCT", "Reusing cached HTML %s", htmlPath.c_str());
  } else {
    if (!Storage.exists(htmlDir.c_str())) Storage.mkdir(htmlDir.c_str());

    // Retry logic for SD card timing issues
    bool streamed = false;
    uint32_t fileSize = 0;
    for (int attempt = 0; attempt < 3 && !streamed; attempt++) {
      if (attempt > 0) {
        LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
        delay(50);  // Brief delay before retry
      }

      // Remove any incomplete file from previous attempt before retrying
      if (Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
      }

      HalFile tmpHtml;
      if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
        continue;
      }
      // Larger chunks mean far fewer SD writes inflating the HTML; a 1KB chunk turned a 584KB
      // single-spine novel into ~570 tiny writes (multi-second). 8KB keeps the transient buffers
      // small while cutting the write count 8x.
      streamed = epub->readItemContentsToStream(localPath, tmpHtml, 8192);
      fileSize = tmpHtml.size();
      // Explicitly close() file before calling Storage.remove()
      tmpHtml.close();

      // If streaming failed, remove the incomplete file immediately
      if (!streamed && Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
        LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
      }
    }

    if (!streamed) {
      LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
      return false;
    }

    LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

    // Promote to the persistent HTML cache immediately -- the inflate is complete and the bytes are
    // valid regardless of whether the layout build finishes, so reopening (even a window-only spine
    // that never finalizes its .bin) skips re-inflation. If the rename fails we just parse the temp.
    if (Storage.rename(tmpHtmlPath.c_str(), htmlPath.c_str())) {
      htmlCached = true;
    } else {
      LOG_DBG("SCT", "Failed to promote HTML cache; parsing from temp");
    }
  }

  const bool opened =
      speculativeMaxPages_ ? openSpeculativeOutput() : Storage.openFileForWrite("SCT", binTmpPath(), file);
  if (!opened) {
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  if (speculativeMaxPages_) {
    if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseSdFontCaches();
  }
  // Header is written with the incomplete-version sentinel; finalizeBuild() commits it.
  if (!writeSectionFileHeader(spec)) {
    discardOpenBuildOutput();
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }

  auto ctx = makeUniqueNoThrow<BuildContext>();
  if (!ctx) {
    LOG_ERR("SCT", "OOM: BuildContext");
    discardOpenBuildOutput();
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  // htmlCached == "htmlPath is the live cache" (reused, or just promoted). finalizeBuild/abandonBuild
  // then leave the cached HTML alone; only an un-promoted temp (rename failed) is theirs to clean up.
  ctx->reusedHtml = htmlCached;
  ctx->htmlPath = htmlPath;
  ctx->tmpHtmlPath = tmpHtmlPath;
  ctx->parsePath = htmlCached ? htmlPath : tmpHtmlPath;

  // Derive the content base directory and image cache path prefix for the parser
  const size_t lastSlash = localPath.find_last_of('/');
  ctx->contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  ctx->imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";

  if (spec.embeddedStyle) {
    ctx->cssParser = epub->getCssParser();
    if (ctx->cssParser) {
      const CssParser::CacheLoadResult cacheResult = ctx->cssParser->loadFromCache();
      if (cacheResult == CssParser::CacheLoadResult::LowMemory) {
        LOG_ERR("SCT", "Insufficient heap to hydrate CSS; section build deferred");
        ctx->cssParser->clear();
        discardOpenBuildOutput();
        if (!ctx->reusedHtml) Storage.remove(ctx->tmpHtmlPath.c_str());
        return false;
      }
      if (cacheResult == CssParser::CacheLoadResult::Invalid) {
        LOG_ERR("SCT", "Failed to load CSS from cache");
      }
    }
  }

  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  // The parser stores the path/contentBase/imageBasePath by reference, so they must
  // live in the BuildContext (which outlives the parser). The page-complete callback
  // captures the BuildContext pointer to append to its in-RAM LUT; build_ owns the
  // context for the parser's whole lifetime.
  BuildContext* ctxPtr = ctx.get();
  ctx->parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
      epub, ctxPtr->parsePath, renderer, spec.fontId, spec.lineCompression, spec.extraParagraphSpacing,
      spec.paragraphAlignment, spec.viewportWidth, spec.viewportHeight, spec.hyphenationEnabled,
      spec.focusReadingEnabled,
      [this, ctxPtr](std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex,
                     const uint32_t visibleTextOffset) {
        ctxPtr->lut.push_back(
            {this->onPageComplete(std::move(page)), paragraphIndex, listItemIndex, visibleTextOffset});
      },
      spec.embeddedStyle, ctxPtr->contentBase, ctxPtr->imageBasePath, spec.imageRendering, std::move(tocAnchors),
      popupFn, ctxPtr->cssParser);
  if (!ctx->parser) {
    LOG_ERR("SCT", "OOM: ChapterHtmlSlimParser");
    if (ctx->cssParser) ctx->cssParser->clear();
    discardOpenBuildOutput();
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }

  ctx->parser->setTextSpacing(spec.characterSpacing, spec.wordSpacingPercent);
  ctx->parser->setParagraphIndentSpaces(spec.paragraphIndentSpaces);
  ctx->parser->setRenderMode(spec.renderMode);
  if (speculativeMaxPages_) ctx->parser->setRejectSpeculativeImages();
  Hyphenator::setPreferredLanguage(epub->getLanguage());
  build_ = std::move(ctx);

  if (!build_->parser->beginParse()) {
    LOG_ERR("SCT", "Failed to begin parse");
    abandonBuild();
    return false;
  }
  build_->totalBytes = build_->parser->parseTotalBytes();
  return true;
}

void Section::discardOpenBuildOutput() {
  if (!file) return;
  const bool closed = file.close();
  if (!speculativeMaxPages_ || (!preserveSpeculativeOutput_ && closed && Storage.monitoredStorageHealthy()))
    Storage.remove(binTmpPath().c_str());
}

bool Section::closeSpeculativeOutput() {
  if (!file) return false;
  speculativePosition_ = file.position();
  speculativeSize_ = file.fileSize64();
  const bool synced = file.sync();
  const bool closed = file.close();
  return synced && closed && Storage.captureMutationMarker(speculativeMarker_);
}

bool Section::reopenSpeculativeOutput() {
  if (file || !Storage.mutationMarkerUnchanged(speculativeMarker_)) return false;
  file = Storage.open(binTmpPath().c_str(), O_RDWR);
  if (!file) return false;
  if (file.isDirectory() || file.fileSize64() != speculativeSize_ || !file.seek64(speculativePosition_)) {
    file.close();
    return false;
  }
  return true;
}

bool Section::buildSomeMore(const int maxPages, bool (*yieldNow)()) {
  if (!speculativeMaxPages_) return buildSomeMoreInternal(maxPages, yieldNow);
  struct Request {
    Section* section;
    int pages;
    bool (*yield)();
  } request{this, maxPages, yieldNow};
  return Storage.withStorageLock(
      [](void* context) {
        const auto& request = *static_cast<Request*>(context);
        auto& section = *request.section;
        if (!section.reopenSpeculativeOutput()) {
          section.preserveSpeculativeOutput_ = true;
          section.abandonBuild();
          return false;
        }
        const bool result = section.buildSomeMoreInternal(request.pages, request.yield);
        if (!section.build_) return result;
        if (section.closeSpeculativeOutput()) return result;
        section.preserveSpeculativeOutput_ = true;
        section.abandonBuild();
        return false;
      },
      &request);
}

bool Section::buildSomeMoreInternal(const int maxPages, bool (*yieldNow)()) {
  if (!build_ || !build_->parser) {
    LOG_ERR("SCT", "buildSomeMore with no active build");
    return false;
  }
  // Pace on pages laid out by THIS build, not pageCount: during a rebuild over a partial,
  // pageCount stays pinned at the partial's watermark until the build passes it, which
  // would otherwise turn one "small" chunk into a blocking rebuild of the whole watermark.
  const int startCount = builtPageCount_;
  for (;;) {
    const auto status = build_->parser->parseStep();
    if (status == ChapterHtmlSlimParser::ParseStatus::Error) {
      LOG_ERR("SCT", "Parse error during incremental build");
      abandonBuild();
      return false;
    }
    if (speculativeMaxPages_ && build_->parser->getAnchors().size() > speculativeMaxAnchors_) {
      abandonBuild();
      return false;
    }
    if (status == ChapterHtmlSlimParser::ParseStatus::Done) {
      return finalizeBuild();
    }
    // ParseStatus::More: yield once we've laid out the requested number of pages, or as soon
    // as the caller has something more urgent to do.
    if ((maxPages > 0 && (builtPageCount_ - startCount) >= maxPages) || (yieldNow && yieldNow())) {
      build_->bytesConsumed = build_->parser->parseBytesConsumed();
      return true;
    }
  }
}

bool Section::hasHtmlCache() const {
  const std::string htmlPath = epub->getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
  return Storage.exists(htmlPath.c_str());
}

bool Section::getHtmlCacheSize(size_t& size) const {
  const std::string htmlPath = epub->getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
  HalFile html;
  if (!Storage.openFileForRead("SCT", htmlPath, html)) return false;
  const size_t bytes = html.size();
  if (!html.close()) return false;
  size = bytes;
  return true;
}

std::optional<uint16_t> Section::findAnchorDuringBuild(const std::string& anchor) const {
  if (!build_ || !build_->parser) return std::nullopt;
  for (const auto& [key, page] : build_->parser->getAnchors()) {
    if (key == anchor) return page;
  }
  return std::nullopt;
}

std::optional<uint16_t> Section::findAnchor(const std::string& anchor) const {
  if (const auto page = findAnchorDuringBuild(anchor)) {
    return page;
  }
  // Fall back to the on-disk anchor map: a finalized section, or a partial whose map
  // covers everything up to its watermark (nullopt past it -- build further and retry).
  return getPageForAnchor(anchor);
}

int Section::getTocIndexForPage(const int page) const {
  int selected = epub->getTocIndexForSpineIndex(spineIndex);
  int latestPage = -1;
  struct TocAnchor {
    size_t hash;
    int index;
  };
  // Bound scratch space to 512 bytes on C3; anchor text stays in the existing caches.
  constexpr size_t BATCH_SIZE = 64;
  auto targets = makeUniqueNoThrow<TocAnchor[]>(BATCH_SIZE);
  if (!targets) {
    LOG_ERR("SCT", "OOM: TOC anchor lookup");
    return selected;
  }
  HalFile anchorFile;
  uint16_t anchorCount = 0;
  std::optional<serialization::BufferedFileReader> anchorReader;
  uint32_t anchorStart = 0;
  if (openAnchorMap(anchorFile, anchorCount)) {
    anchorStart = anchorFile.position();
    // Reuse one 1 KB buffer across batches and interleaved TOC metadata reads.
    anchorReader.emplace(anchorFile, 1024);
  }
  std::string cachedAnchor;
  size_t count = 0;
  const auto considerAnchor = [&](const std::string& anchor, const uint16_t anchorPage, const bool fromDisk) {
    const size_t hash = std::hash<std::string>{}(anchor);
    for (size_t i = 0; i < count; ++i) {
      if (targets[i].hash != hash || epub->getTocItem(targets[i].index).anchor != anchor) continue;
      const int resolvedPage = fromDisk ? findAnchorDuringBuild(anchor).value_or(anchorPage) : anchorPage;
      if (resolvedPage <= page &&
          (resolvedPage > latestPage || (resolvedPage == latestPage && targets[i].index > selected))) {
        selected = targets[i].index;
        latestPage = resolvedPage;
      }
    }
  };
  const auto resolveBatch = [&]() {
    if (build_ && build_->parser) {
      for (const auto& [anchor, anchorPage] : build_->parser->getAnchors()) {
        considerAnchor(anchor, anchorPage, false);
      }
    }
    if (anchorReader && anchorReader->seek(anchorStart)) {
      for (uint16_t i = 0; i < anchorCount; ++i) {
        uint16_t anchorPage;
        serialization::readString(*anchorReader, cachedAnchor);
        serialization::readPod(*anchorReader, anchorPage);
        considerAnchor(cachedAnchor, anchorPage, true);
      }
    }
    count = 0;
  };
  const int first = std::max(0, selected);
  for (int i = first; i < epub->getTocItemsCount(); ++i) {
    const auto item = epub->getTocItem(i);
    if (item.spineIndex != spineIndex) continue;
    if (item.anchor.empty()) {
      if (page >= 0 && latestPage <= 0) {
        selected = i;
        latestPage = 0;
      }
    } else {
      targets[count++] = {std::hash<std::string>{}(item.anchor), i};
      if (count == BATCH_SIZE) resolveBatch();
    }
  }
  if (count > 0) resolveBatch();
  return selected;
}

uint16_t Section::estimatedTotalPages() const {
  // Extrapolation from a suspended session's watermark trailer. A static snapshot, so no EMA
  // damping is needed. Also the best guess while a rebuild is running but hasn't laid out
  // enough pages yet to extrapolate from its own progress.
  const auto partialEstimate = [this]() -> uint16_t {
    if (!partial_ || partialBytesConsumed_ == 0 || partialTotalBytes_ <= partialBytesConsumed_) {
      return pageCount;
    }
    const uint64_t est = static_cast<uint64_t>(partialPageCount_) * partialTotalBytes_ / partialBytesConsumed_;
    if (est <= pageCount) return pageCount;
    return est > 60000 ? 60000 : static_cast<uint16_t>(est);
  };

  if (!build_) {
    return partial_ ? partialEstimate() : pageCount;  // partial -> extrapolate, finalized -> exact
  }
  const uint32_t consumed = build_->bytesConsumed;
  const uint32_t total = build_->totalBytes;
  if (builtPageCount_ == 0 || consumed == 0 || total <= consumed) return partialEstimate();

  // Raw extrapolation: scale the pages built so far by the fraction of HTML still unparsed. This
  // re-derives from a growing, non-uniform sample, so it jitters up and down as the build crosses
  // dense vs sparse regions of the chapter.
  const uint64_t raw = static_cast<uint64_t>(builtPageCount_) * total / consumed;

  // Damp that jitter with an exponential moving average. Step it once per build advance (keyed on
  // bytesConsumed) rather than per status-bar redraw, so the smoothing rate doesn't depend on how
  // often we repaint. As the build nears the end, consumed -> total and raw -> the built count, so
  // the average settles onto the true count (and finalizeBuild then returns the exact pageCount).
  constexpr float ALPHA = 0.25f;  // weight of each new sample; lower = steadier but slower to settle
  if (build_->smoothedEstimate <= 0) {
    build_->smoothedEstimate = static_cast<float>(raw);  // seed on the first estimate
  } else if (consumed != build_->smoothedAtConsumed) {
    build_->smoothedEstimate += ALPHA * (static_cast<float>(raw) - build_->smoothedEstimate);
  }
  build_->smoothedAtConsumed = consumed;

  const uint64_t est = static_cast<uint64_t>(build_->smoothedEstimate + 0.5f);
  if (est <= pageCount) return pageCount;  // never fewer than the pages already available
  return est > 60000 ? 60000 : static_cast<uint16_t>(est);
}

// Write the LUTs and anchor map into the open tmp .bin, patch the header with the built
// page count and table offsets, stamp `version` as the commit point, then swap the tmp
// file over filePath. For SECTION_FILE_PARTIAL_VERSION a watermark trailer
// (bytesConsumed, totalBytes) is appended after the li LUT so a later open can estimate
// the total page count. The parser must still be alive (anchors are read from it).
// On failure the tmp is removed and any pre-existing file at filePath is left intact.
bool Section::commitBuildFile(const uint8_t version, const uint32_t bytesConsumed, const uint32_t totalBytes) {
  const bool asPartial = (version == SECTION_FILE_PARTIAL_VERSION);

  const auto failCommit = [this]() {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    discardOpenBuildOutput();
    return false;
  };
  bool writeOk = true;
  const auto writePod = [this, &writeOk](const auto& value) {
    if (!writeOk) return;
    writeOk = serialization::tryWritePod(file, value);
  };
  const auto writeString = [this, &writeOk](const std::string& value) {
    if (!writeOk) return;
    writeOk = serialization::tryWriteString(file, value);
  };

  const uint32_t lutOffset = file.position();
  for (const auto& entry : build_->lut) {
    if (entry.fileOffset == 0) {
      LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
      return failCommit();
    }
    writePod(entry.fileOffset);
  }

  // Write anchor-to-page map for fragment navigation (e.g. footnote targets). For a
  // partial, skip anchors that landed on the incomplete trailing page the suspend drops.
  const uint32_t anchorMapOffset = file.position();
  const auto& anchors = build_->parser->getAnchors();
  uint16_t anchorCount = 0;
  for (const auto& [anchor, page] : anchors) {
    if (!asPartial || page < builtPageCount_) anchorCount++;
  }
  writePod(anchorCount);
  for (const auto& [anchor, page] : anchors) {
    if (asPartial && page >= builtPageCount_) continue;
    writeString(anchor);
    writePod(page);
  }

  const uint32_t paragraphLutOffset = file.position();
  writePod(static_cast<uint16_t>(build_->lut.size()));
  for (const auto& entry : build_->lut) {
    writePod(entry.paragraphIndex);
  }

  const uint32_t liLutFileOffset = static_cast<uint32_t>(file.position());
  for (const auto& entry : build_->lut) {
    writePod(entry.listItemIndex);
  }

  const uint32_t visibleLutFileOffset = static_cast<uint32_t>(file.position());
  for (const auto& entry : build_->lut) {
    writePod(entry.visibleTextOffset);
  }

  if (asPartial) {
    // Watermark trailer, located on load immediately after the visible-offset LUT.
    writePod(bytesConsumed);
    writePod(totalBytes);
  }

  // Patch header with the built page count and section offsets...
  if (!writeOk || !file.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(builtPageCount_))) return failCommit();
  writePod(builtPageCount_);
  writePod(lutOffset);
  writePod(anchorMapOffset);
  writePod(paragraphLutOffset);
  writePod(liLutFileOffset);
  writePod(visibleLutFileOffset);
  // ...then commit by overwriting the sentinel version with the real one. Writing the
  // version last makes it the commit point: a crash before here leaves version 0.
  if (!writeOk || (speculativeMaxPages_ && (!Storage.monitoredStorageHealthy() || !file.sync())) || !file.seek(0))
    return failCommit();
  if (speculativeMaxPages_ && !Storage.monitoredStorageHealthy()) return failCommit();
  writePod(version);
  if (!writeOk) return failCommit();
  // Explicit close() required: member variable persists beyond function scope
  const bool synced = !speculativeMaxPages_ || file.sync();
  const bool closed = file.close();
  if (speculativeMaxPages_ && (!synced || !closed || !Storage.monitoredStorageHealthy())) {
    preserveSpeculativeOutput_ = true;
    return false;
  }
  // Speculation creates the destination exclusively up front. No rename is
  // needed, so an interrupted publication cannot create two aliased names.
  if (speculativeMaxPages_) return Storage.lookupPath(speculativeFilePath().c_str()) == HalStorage::PathStatus::Present;

  // Swap into place. A crash between remove and rename loses the old file but keeps a
  // fully-committed tmp; the next build just removes it and rebuilds.
  if (Storage.exists(filePath.c_str())) {
    Storage.remove(filePath.c_str());
  }
  if (!Storage.rename(binTmpPath().c_str(), filePath.c_str())) {
    LOG_ERR("SCT", "Failed to move built section into place");
    if (!speculativeMaxPages_) Storage.remove(binTmpPath().c_str());
    return false;
  }
  return true;
}

bool Section::finalizeBuild() {
  // Flush the trailing page (emits the last page via the completePageFn into the LUT).
  // A false return means layout dropped content (OOM); committing would persist a
  // section cache with holes in the text, so abandon the build instead.
  if (!build_->parser->finishParse()) {
    LOG_ERR("SCT", "Parse finalize failed; abandoning section build");
    abandonBuild();
    return false;
  }

  if (speculativeMaxPages_ &&
      (builtPageCount_ > speculativeMaxPages_ || build_->parser->getAnchors().size() > speculativeMaxAnchors_)) {
    abandonBuild();
    return false;
  }

  if (!build_->reusedHtml) {
    // Parse succeeded: promote the freshly unzipped HTML to the persistent cache so future
    // rebuilds skip zip inflation. If promotion fails, drop the temp -- the build still succeeded.
    if (!Storage.rename(build_->tmpHtmlPath.c_str(), build_->htmlPath.c_str())) {
      LOG_DBG("SCT", "Failed to promote HTML cache, removing temp");
      Storage.remove(build_->tmpHtmlPath.c_str());
    }
  }

  const bool committed = speculativeMaxPages_
                             ? Storage.withStorageLock(
                                   [](void* context) {
                                     auto* section = static_cast<Section*>(context);
                                     return Storage.lookupPath(section->speculativeFilePath().c_str()) ==
                                                HalStorage::PathStatus::Present &&
                                            section->commitBuildFile(SECTION_FILE_VERSION, 0, 0);
                                   },
                                   this)
                             : commitBuildFile(SECTION_FILE_VERSION, 0, 0);
  if (!committed && speculativeMaxPages_) {
    abandonBuild();
    return false;
  }
  if (build_->cssParser) build_->cssParser->clear();
  build_.reset();
  if (!committed) {
    // commitBuildFile removed filePath before the failed swap, so nothing valid remains.
    partial_ = false;
    partialPageCount_ = 0;
    pageCount = 0;
    builtPageCount_ = 0;
    return false;
  }
  buildComplete_ = true;
  partial_ = false;
  partialPageCount_ = 0;
  pageCount = builtPageCount_;
  return true;
}

void Section::suspendBuild() {
  if (!build_) return;
  if (speculativeMaxPages_) {
    abandonBuild();
    return;
  }

  // Only worth persisting if this build produced pages a pre-existing partial doesn't
  // already cover; otherwise keep the older (bigger) partial and just drop the tmp.
  const bool worthKeeping = builtPageCount_ > 0 && (!partial_ || builtPageCount_ > partialPageCount_);

  bool committed = false;
  if (worthKeeping) {
    // Capture the parse watermark and commit before tearing the parser down (the anchor
    // map is read from it). The incomplete trailing page is intentionally not flushed:
    // only fully laid-out pages are persisted, and the rebuild re-derives the rest.
    const uint32_t consumed = static_cast<uint32_t>(build_->parser->parseBytesConsumed());
    committed = commitBuildFile(SECTION_FILE_PARTIAL_VERSION, consumed, build_->totalBytes);
    if (committed) {
      partial_ = true;
      partialPageCount_ = builtPageCount_;
      partialBytesConsumed_ = consumed;
      partialTotalBytes_ = build_->totalBytes;
      LOG_INF("SCT", "Suspended build: %u pages persisted", builtPageCount_);
    }
  }

  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (!committed && file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_.reset();
  buildComplete_ = false;
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

void Section::abandonBuild() {
  if (!build_) return;
  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (!file && speculativeMaxPages_ && !preserveSpeculativeOutput_) {
    Storage.withStorageLock(
        [](void* context) {
          auto* section = static_cast<Section*>(context);
          if (!Storage.mutationMarkerUnchanged(section->speculativeMarker_)) return false;
          const std::string tmp = section->binTmpPath();
          return Storage.lookupPath(tmp.c_str()) == HalStorage::PathStatus::Present && Storage.remove(tmp.c_str());
        },
        this);
  }
  if (file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    discardOpenBuildOutput();
  }
  // A parse error would recur against the same HTML, so drop any partial too -- resuming
  // from it would just re-enter the failing build every open.
  if (!speculativeMaxPages_ && Storage.exists(filePath.c_str())) {
    Storage.remove(filePath.c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_.reset();
  buildComplete_ = false;
  if (!speculativeMaxPages_) {
    partial_ = false;
    partialPageCount_ = 0;
  }
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

std::unique_ptr<Page> Section::loadPageDuringBuild(const int page) {
  if (!build_ || page < 0 || page >= static_cast<int>(build_->lut.size()) || !file) {
    return nullptr;
  }
  const uint32_t pos = build_->lut[page].fileOffset;
  if (pos == 0) {
    return nullptr;
  }
  // The .bin is open O_RDWR for the build. Read the already-written page, then restore
  // the write cursor so the next onPageComplete keeps appending where it left off.
  const uint32_t writePos = file.position();
  file.seek(pos);
  auto p = Page::deserialize(file);
  file.seek(writePos);
  if (p) {
    p->visibleTextOffset = build_->lut[page].visibleTextOffset;
  }
  return p;
}

// Read a page from the committed file at filePath (finalized section or partial from a
// previous session). Uses a local handle so it is safe while a build holds the member
// `file` open on the tmp .bin.
std::unique_ptr<Page> Section::loadPageAt(const int page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return nullptr;
  }

  const uint64_t fileSize = f.size();
  uint32_t lutOffset = 0, anchorOffset = 0, paragraphOffset = 0, listOffset = 0, visibleLutOffset = 0;
  if (page < 0 || fileSize < HEADER_SIZE || !f.seek(HEADER_SIZE - sizeof(uint32_t) * 5) ||
      !serialization::tryReadPod(f, lutOffset) || !serialization::tryReadPod(f, anchorOffset) ||
      !serialization::tryReadPod(f, paragraphOffset) || !serialization::tryReadPod(f, listOffset) ||
      !serialization::tryReadPod(f, visibleLutOffset) || lutOffset < HEADER_SIZE)
    return nullptr;
  const uint64_t pages = partial_ ? partialPageCount_ : pageCount;
  if (static_cast<uint64_t>(page) >= pages ||
      static_cast<uint64_t>(lutOffset) + pages * sizeof(uint32_t) > anchorOffset ||
      static_cast<uint64_t>(anchorOffset) + sizeof(uint16_t) > paragraphOffset ||
      static_cast<uint64_t>(paragraphOffset) + sizeof(uint16_t) + pages * sizeof(uint16_t) > listOffset ||
      static_cast<uint64_t>(listOffset) + pages * sizeof(uint16_t) > visibleLutOffset ||
      static_cast<uint64_t>(visibleLutOffset) + pages * sizeof(uint32_t) > fileSize)
    return nullptr;
  uint16_t anchorCount = 0;
  if (!f.seek(anchorOffset) || !serialization::tryReadPod(f, anchorCount) ||
      anchorCount > (static_cast<uint64_t>(paragraphOffset) - anchorOffset - sizeof(uint16_t)) /
                        (sizeof(uint32_t) + sizeof(uint16_t)))
    return nullptr;
  for (uint16_t i = 0; i < anchorCount; ++i) {
    uint32_t length = 0;
    uint16_t anchorPage = 0;
    if (!serialization::tryReadPod(f, length)) return nullptr;
    const uint64_t afterString = static_cast<uint64_t>(f.position()) + length;
    if (afterString + sizeof(anchorPage) > paragraphOffset || !f.seek(afterString) ||
        !serialization::tryReadPod(f, anchorPage))
      return nullptr;
  }
  uint16_t paragraphCount = 0;
  if (f.position() != paragraphOffset || !serialization::tryReadPod(f, paragraphCount) || paragraphCount != pages)
    return nullptr;
  const uint64_t lutEntry = static_cast<uint64_t>(lutOffset) + sizeof(uint32_t) * static_cast<uint64_t>(page);
  uint32_t pagePos = 0;
  if (lutEntry + sizeof(uint32_t) > fileSize || !f.seek(lutEntry) || !serialization::tryReadPod(f, pagePos) ||
      pagePos < HEADER_SIZE || pagePos >= lutOffset)
    return nullptr;

  // Read this page's visible-codepoint start offset from the visible-offset LUT (last header slot)
  // in the same open handle, so the reader can persist progress without reopening the section file
  // on every page turn (see Page::visibleTextOffset). A malformed/old file leaves it at 0.
  uint32_t visibleTextOffset = 0;
  const uint64_t visibleEntry =
      static_cast<uint64_t>(visibleLutOffset) + sizeof(uint32_t) * static_cast<uint64_t>(page);
  if (!f.seek(visibleEntry) || !serialization::tryReadPod(f, visibleTextOffset)) return nullptr;

  if (!f.seek(pagePos)) return nullptr;
  auto p = Page::deserialize(f);
  if (p) {
    p->visibleTextOffset = visibleTextOffset;
  }
  return p;
  // No f.close() needed -- DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}

std::unique_ptr<Page> Section::loadPage(const int page) {
  if (page < 0) {
    return nullptr;
  }
  if (build_ && page < static_cast<int>(build_->lut.size())) {
    return loadPageDuringBuild(page);
  }
  // Not (yet) in the active build: serve from the file on disk -- a finalized section,
  // or a partial from a previous session whose pages the rebuild hasn't reached again.
  const int onDisk = partial_ ? partialPageCount_ : (build_ ? 0 : pageCount);
  if (page >= onDisk) {
    return nullptr;
  }
  return loadPageAt(page);
}

std::string Section::getTextFromSectionFile() {
  std::string fullText;
  auto p = loadPage(currentPage);
  if (p) {
    for (const auto& el : p->elements) {
      if (el->getTag() == TAG_PageLine) {
        const auto& line = static_cast<const PageLine&>(*el);
        if (line.getBlock()) {
          const auto& block = *line.getBlock();
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!fullText.empty()) fullText += " ";
            fullText += block.wordText(i);
          }
        }
      }
    }
  }
  return fullText;
}

std::optional<uint16_t> Section::getCachedPageCount() const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (fileSize < HEADER_SIZE) {
    return std::nullopt;
  }

  // Only a finalized section's count is the chapter total; a partial's count is just the
  // suspended build's watermark, which would skew progress mapping. Callers fall back to
  // their own estimates.
  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t));
  uint16_t count;
  serialization::readPod(f, count);
  return count;
}

bool Section::openAnchorMap(HalFile& f, uint16_t& count) const {
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return false;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 4);
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    return false;
  }

  f.seek(anchorMapOffset);
  serialization::readPod(f, count);
  return true;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile f;
  uint16_t count;
  if (!openAnchorMap(f, count)) return std::nullopt;
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page;
    serialization::readString(f, key);
    serialization::readPod(f, page);
    if (key == anchor) {
      return page;
    }
  }

  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = paragraphLutOffset + sizeof(uint16_t) + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pagePIdx;
    serialization::readPod(f, pagePIdx);
    if (pagePIdx >= pIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0 || page >= count) {
    return std::nullopt;
  }

  const uint32_t entryEnd = paragraphLutOffset + sizeof(uint16_t) + (page + 1) * sizeof(uint16_t);
  if (entryEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset + sizeof(uint16_t) + page * sizeof(uint16_t));
  uint16_t pIdx;
  serialization::readPod(f, pIdx);
  return pIdx;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t liLutOffset;
  serialization::readPod(f, liLutOffset);
  if (liLutOffset == 0 || liLutOffset >= fileSize) {
    return std::nullopt;
  }

  // The li LUT shares count with the paragraph LUT; read count from paragraphLutOffset
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = liLutOffset + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(liLutOffset);
  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pageLiIdx;
    serialization::readPod(f, pageLiIdx);
    if (pageLiIdx >= liIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint32_t> Section::getVisibleTextOffsetForPage(const uint16_t page) const {
  if (build_ && page < build_->lut.size()) {
    return build_->lut[page].visibleTextOffset;
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t));
  uint16_t count;
  serialization::readPod(f, count);
  if (page >= count) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t visibleLutOffset;
  serialization::readPod(f, visibleLutOffset);
  const uint32_t entryOffset = visibleLutOffset + static_cast<uint32_t>(page) * sizeof(uint32_t);
  if (visibleLutOffset < HEADER_SIZE || entryOffset + sizeof(uint32_t) > f.size()) {
    return std::nullopt;
  }

  f.seek(entryOffset);
  uint32_t result;
  serialization::readPod(f, result);
  return result;
}

std::optional<uint16_t> Section::getPageForVisibleTextOffset(const uint32_t offset,
                                                             const bool preferFirstAtOffset) const {
  const auto findInEntries = [offset, preferFirstAtOffset](const auto& entries) -> std::optional<uint16_t> {
    if (entries.empty()) return std::nullopt;
    uint16_t result = 0;
    for (size_t i = 0; i < entries.size(); i++) {
      const uint32_t pageStart = entries[i].visibleTextOffset;
      if (preferFirstAtOffset && pageStart == offset) {
        return static_cast<uint16_t>(i);
      }
      if (pageStart > offset) break;
      result = static_cast<uint16_t>(i);
    }
    return result;
  };

  if (build_ && !build_->lut.empty()) {
    // Resolve within the active build's known range. Later offsets may still be
    // covered by an on-disk partial that the resumed build has not reached yet.
    if (offset <= build_->lut.back().visibleTextOffset) {
      return findInEntries(build_->lut);
    }
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }
  const bool partial = version == SECTION_FILE_PARTIAL_VERSION;

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t));
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t visibleLutOffset;
  serialization::readPod(f, visibleLutOffset);
  if (visibleLutOffset < HEADER_SIZE || visibleLutOffset + static_cast<uint32_t>(count) * sizeof(uint32_t) > f.size()) {
    return std::nullopt;
  }

  if (!f.seek(visibleLutOffset)) {
    LOG_ERR("SCT", "Failed to seek visible-text LUT");
    return std::nullopt;
  }
  uint16_t result = 0;
  uint32_t lastPageStart = 0;
  constexpr uint32_t BATCH_PAGE_COUNT = 32;
  uint32_t starts[BATCH_PAGE_COUNT];  // 128-byte stack buffer; offsets stay in file order for ties.
  for (uint32_t base = 0; base < count; base += BATCH_PAGE_COUNT) {
    const uint32_t batch = std::min<uint32_t>(BATCH_PAGE_COUNT, count - base);
    const size_t bytes = batch * sizeof(uint32_t);
    if (f.read(starts, bytes) != static_cast<int>(bytes)) {
      LOG_ERR("SCT", "Failed to read visible-text LUT batch");
      return std::nullopt;
    }
    for (uint32_t i = 0; i < batch; ++i) {
      lastPageStart = starts[i];
      if (lastPageStart > offset) return result;
      result = static_cast<uint16_t>(base + i);
      if (preferFirstAtOffset && lastPageStart == offset) return result;
    }
  }
  if (partial && offset > lastPageStart) {
    return std::nullopt;
  }
  return result;
}
