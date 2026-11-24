#include "ImageBlock.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#if FREEINK_DEVICE_X4PRO
#include <HalMemory.h>
#endif
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

#include "Epub/converters/DirectPixelWriter.h"
#include "Epub/converters/ImageDecoderFactory.h"

// Cache file format:
// - uint16_t width
// - uint16_t height
// - uint8_t pixels[...] - 2 bits per pixel, packed (4 pixels per byte), row-major order

void* ImageBlock::extractCtx = nullptr;
ImageBlock::ExtractFn ImageBlock::extractFn = nullptr;

void ImageBlock::setExtractor(void* ctx, ExtractFn fn) {
  extractCtx = ctx;
  extractFn = fn;
}

namespace {

std::string getCachePath(const std::string& imagePath) {
  // Replace extension with .pxc (pixel cache)
  size_t dotPos = imagePath.rfind('.');
  if (dotPos != std::string::npos) {
    return imagePath.substr(0, dotPos) + ".pxc";
  }
  return imagePath + ".pxc";
}

bool readValidCacheHeader(HalFile& cacheFile, const int expectedWidth, const int expectedHeight, uint16_t& cachedWidth,
                          uint16_t& cachedHeight) {
  if (cacheFile.read(&cachedWidth, 2) != 2 || cacheFile.read(&cachedHeight, 2) != 2) {
    return false;
  }
  if (cachedWidth == 0 || cachedHeight == 0) return false;

  const int widthDiff = abs(cachedWidth - expectedWidth);
  const int heightDiff = abs(cachedHeight - expectedHeight);
  if (widthDiff > 1 || heightDiff > 1) {
    return false;
  }

  const size_t bytesPerRow = (cachedWidth + 3) / 4;
  const size_t expectedSize = 4 + bytesPerRow * cachedHeight;
  return cacheFile.size() >= expectedSize;
}

// Suppress repeated failures across the BW/grayscale passes of one page render.
// Clear before the next page render so transient memory/storage failures retry.
constexpr size_t MAX_RENDER_IMAGE_FAILURES = 16;
uint64_t failedImageHashes[MAX_RENDER_IMAGE_FAILURES];
size_t failedImageCount = 0;

uint64_t imagePathHash(const std::string& path) {
  uint64_t hash = 14695981039346656037ull;
  for (const char c : path) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool imageFailedThisRender(const std::string& path) {
  const uint64_t hash = imagePathHash(path);
  for (size_t i = 0; i < failedImageCount; i++) {
    if (failedImageHashes[i] == hash) return true;
  }
  return false;
}

void rememberImageFailure(const std::string& path) {
  if (failedImageCount == MAX_RENDER_IMAGE_FAILURES || imageFailedThisRender(path)) return;
  failedImageHashes[failedImageCount++] = imagePathHash(path);
}

// Reuse packed pixels between the BW and grayscale passes of one page.
// On the X4 Pro, several images can share the existing 96 KiB PSRAM budget.
// If no PSRAM slot is populated, the heap can still cache one image in chunks.
// Additional images stream from SD without evicting the cached images.
constexpr size_t PXC_CHUNK_SHIFT = 14;  // 16 KiB chunks
constexpr size_t PXC_CHUNK_SIZE = 1u << PXC_CHUNK_SHIFT;
constexpr size_t PXC_MAX_CHUNKS = 6;
constexpr size_t PXC_MAX_BYTES = PXC_CHUNK_SIZE * PXC_MAX_CHUNKS;
constexpr size_t PXC_HEAP_RESERVE = 24 * 1024;
constexpr size_t PXC_MAX_ALLOC_RESERVE = 8 * 1024;
// Rows can straddle a heap chunk boundary. This buffer fits one packed row.
constexpr int PXC_MAX_BYTES_PER_ROW = 208;

struct PxcKey {
  uint64_t hash = 0;
  uint16_t width = 0;
  uint16_t height = 0;

  bool matches(uint64_t cacheHash, int expectedWidth, int expectedHeight) const {
    return width != 0 && hash == cacheHash && abs(width - expectedWidth) <= 1 && abs(height - expectedHeight) <= 1;
  }
};

std::unique_ptr<uint8_t[]> pxcChunks[PXC_MAX_CHUNKS];
PxcKey pxcChunkKey;

#if FREEINK_DEVICE_X4PRO
struct PsramPxcSlot {
  HalMemory::PsramBuffer pixels;
  PxcKey key;
};

constexpr size_t PXC_MAX_PSRAM_SLOTS = 4;
PsramPxcSlot pxcPsramSlots[PXC_MAX_PSRAM_SLOTS];
size_t pxcPsramCount = 0;
size_t pxcPsramBytes = 0;
#endif

void releasePxcChunks() {
  for (auto& chunk : pxcChunks) chunk.reset();
  pxcChunkKey = {};
}

void releasePxcSlot() {
  releasePxcChunks();
#if FREEINK_DEVICE_X4PRO
  for (size_t i = 0; i < pxcPsramCount; i++) pxcPsramSlots[i] = {};
  pxcPsramCount = 0;
  pxcPsramBytes = 0;
#endif
}

const uint8_t* pxcRowPtr(size_t rowStart, int bytesPerRow, uint8_t* tempRow) {
  const size_t chunk = rowStart >> PXC_CHUNK_SHIFT;
  const size_t offset = rowStart & (PXC_CHUNK_SIZE - 1);
  if (offset + bytesPerRow <= PXC_CHUNK_SIZE) {
    return pxcChunks[chunk].get() + offset;
  }
  const size_t firstPart = PXC_CHUNK_SIZE - offset;
  memcpy(tempRow, pxcChunks[chunk].get() + offset, firstPart);
  memcpy(tempRow + firstPart, pxcChunks[chunk + 1].get(), bytesPerRow - firstPart);
  return tempRow;
}

// cacheFile is positioned just past the header. The caller checks the budget.
bool loadPxcChunks(const PxcKey& key, HalFile& cacheFile, size_t remaining) {
  for (size_t i = 0; remaining != 0; i++) {
    const size_t want = std::min(remaining, PXC_CHUNK_SIZE);
    if (ESP.getFreeHeap() < remaining + PXC_HEAP_RESERVE || ESP.getMaxAllocHeap() < want + PXC_MAX_ALLOC_RESERVE) {
      releasePxcChunks();
      return false;
    }
    pxcChunks[i] = makeUniqueNoThrow<uint8_t[]>(want);
    if (!pxcChunks[i] || cacheFile.read(pxcChunks[i].get(), want) != static_cast<int>(want)) {
      releasePxcChunks();
      return false;
    }
    remaining -= want;
  }
  pxcChunkKey = key;
  return true;
}

void renderPackedRow(DirectPixelWriter& writer, const uint8_t* pixels, int x, int y, int width) {
  writer.beginRow(y);
  int first, last;
  writer.bandColRange(x, width, first, last);
  for (int col = first; col < last; col++) {
    const int bitShift = 6 - (col & 3) * 2;
    const uint8_t level = (pixels[col >> 2] >> bitShift) & 0x03;
    writer.writePixel(x + col, level);
  }
}

// Dithered images have already contributed to BW. Their pixel writer writes
// neither text-AA plane. Cached rows can skip unpacking in those passes.
bool cachePassWritesPixels(const GfxRenderer& renderer) {
  return renderer.getRenderMode() == GfxRenderer::BW || !renderer.imagesDitheredInBw();
}

template <typename ReadRow>
void renderPxcRows(GfxRenderer& renderer, int x, int y, const PxcKey& key, const ReadRow& readRow) {
  if (!cachePassWritesPixels(renderer)) return;
  const int bytesPerRow = (key.width + 3) / 4;
  DirectPixelWriter pw;
  pw.init(renderer);

  const auto [first, last] = pw.bandRowRange(y, key.height);
  for (int row = first; row < last; row++) {
    const auto* pixels = readRow(static_cast<size_t>(row) * bytesPerRow, bytesPerRow);
    renderPackedRow(pw, pixels, x, y + row, key.width);
  }
}

void renderPxcChunks(GfxRenderer& renderer, int x, int y) {
  uint8_t tempRow[PXC_MAX_BYTES_PER_ROW];
  renderPxcRows(renderer, x, y, pxcChunkKey,
                [&](size_t offset, int bytesPerRow) { return pxcRowPtr(offset, bytesPerRow, tempRow); });
}

#if FREEINK_DEVICE_X4PRO
void renderPxcPsram(GfxRenderer& renderer, int x, int y, const PsramPxcSlot& slot) {
  renderPxcRows(renderer, x, y, slot.key, [&](size_t offset, int) { return slot.pixels.get() + offset; });
}
#endif

bool loadAndRenderPxcRam(GfxRenderer& renderer, int x, int y, HalFile& cacheFile, const PxcKey& key, int bytesPerRow) {
  const size_t bytes = static_cast<size_t>(bytesPerRow) * key.height;
  if (pxcChunkKey.width || bytesPerRow > PXC_MAX_BYTES_PER_ROW || bytes > PXC_MAX_BYTES) return false;

#if FREEINK_DEVICE_X4PRO
  if (pxcPsramCount < PXC_MAX_PSRAM_SLOTS && bytes <= PXC_MAX_BYTES - pxcPsramBytes) {
    if (auto pixels = HalMemory::allocatePsram(bytes)) {
      for (size_t offset = 0; offset < bytes; offset += PXC_CHUNK_SIZE) {
        const size_t want = std::min(bytes - offset, PXC_CHUNK_SIZE);
        if (cacheFile.read(pixels.get() + offset, want) != static_cast<int>(want)) return false;
      }
      auto& slot = pxcPsramSlots[pxcPsramCount++];
      slot = {std::move(pixels), key};
      pxcPsramBytes += bytes;
      renderPxcPsram(renderer, x, y, slot);
      LOG_DBG("IMG", "Cache render complete (%u bytes in PSRAM)", static_cast<unsigned>(bytes));
      return true;
    }
  }
  if (pxcPsramCount != 0) return false;
#endif

  if (!loadPxcChunks(key, cacheFile, bytes)) return false;
  renderPxcChunks(renderer, x, y);
  LOG_DBG("IMG", "Cache render complete (payload now in RAM)");
  return true;
}

bool renderFromCache(GfxRenderer& renderer, const std::string& cachePath, int x, int y, int expectedWidth,
                     int expectedHeight) {
  // A later pass of the same page render: the payload is already in RAM, skip
  // the file entirely.
  const uint64_t cacheHash = imagePathHash(cachePath);
#if FREEINK_DEVICE_X4PRO
  for (size_t i = 0; i < pxcPsramCount; i++) {
    const auto& slot = pxcPsramSlots[i];
    if (slot.key.matches(cacheHash, expectedWidth, expectedHeight)) {
      renderPxcPsram(renderer, x, y, slot);
      return true;
    }
  }
#endif
  if (pxcChunkKey.matches(cacheHash, expectedWidth, expectedHeight)) {
    renderPxcChunks(renderer, x, y);
    return true;
  }

  HalFile cacheFile;
  if (!Storage.openFileForRead("IMG", cachePath, cacheFile)) {
    return false;
  }

  uint16_t cachedWidth, cachedHeight;
  if (!readValidCacheHeader(cacheFile, expectedWidth, expectedHeight, cachedWidth, cachedHeight)) {
    LOG_ERR("IMG", "Invalid image cache: %s", cachePath.c_str());
    return false;
  }

  LOG_DBG("IMG", "Loading from cache: %s (%dx%d)", cachePath.c_str(), cachedWidth, cachedHeight);

  const int bytesPerRow = (cachedWidth + 3) / 4;  // 2 bits per pixel, 4 pixels per byte

  if (loadAndRenderPxcRam(renderer, x, y, cacheFile, {cacheHash, cachedWidth, cachedHeight}, bytesPerRow)) return true;

  DirectPixelWriter pw;
  pw.init(renderer);
  const auto [first, last] = pw.bandRowRange(y, cachedHeight);
  if (first == last) return true;

  // A failed RAM fill may have advanced the file. Seek to the first row the
  // current pass uses, leaving other landscape strips unread.
  if (!cacheFile.seek(4 + static_cast<size_t>(first) * bytesPerRow)) {
    LOG_ERR("IMG", "Could not seek to pixel cache row %d", first);
    return false;
  }

  // Read several rows per SD access. A one-row-per-read loop here means
  // cachedHeight (~728) tiny reads through the storage mutex + SdFat; batching
  // rows into a ~4KB buffer cuts that to ~20 reads per pass without holding the
  // whole image.
  int rowsPerRead = std::clamp(4096 / bytesPerRow, 1, last - first);
  auto readBuffer = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(rowsPerRead) * bytesPerRow);
  if (!readBuffer) {
    // Fall back to a single-row buffer under memory pressure.
    rowsPerRead = 1;
    readBuffer = makeUniqueNoThrow<uint8_t[]>(bytesPerRow);
  }
  if (!readBuffer) {
    LOG_ERR("IMG", "Failed to allocate row buffer");
    return false;
  }

  // The streaming fallback still checks each SD read so an unreadable cache
  // takes the usual decode or placeholder path.
  const bool paint = cachePassWritesPixels(renderer);
  for (int row = first; row < last;) {
    const int count = std::min(last - row, rowsPerRead);
    const size_t bytes = static_cast<size_t>(count) * bytesPerRow;
    if (cacheFile.read(readBuffer.get(), bytes) != static_cast<int>(bytes)) {
      LOG_ERR("IMG", "Cache read error at row %d", row);
      return false;
    }
    if (paint) {
      for (int i = 0; i < count; i++) {
        const auto* pixels = readBuffer.get() + static_cast<size_t>(i) * bytesPerRow;
        renderPackedRow(pw, pixels, x, y + row + i, cachedWidth);
      }
    }
    row += count;
  }

  LOG_DBG("IMG", "Cache render complete");
  return true;
}

}  // namespace

bool ImageBlock::hasValidCache() const {
  const auto cachePath = getCachePath(imagePath);
  HalFile cacheFile;
  if (!Storage.openFileForRead("IMG", cachePath, cacheFile)) {
    return false;
  }

  uint16_t cachedWidth, cachedHeight;
  return readValidCacheHeader(cacheFile, width, height, cachedWidth, cachedHeight);
}

bool ImageBlock::needsDecode() const { return !imageFailedThisRender(imagePath) && !hasValidCache(); }

void ImageBlock::clearRenderFailures() { failedImageCount = 0; }

void ImageBlock::releaseRenderCache() { releasePxcSlot(); }

void ImageBlock::renderPlaceholder(GfxRenderer& renderer, const int x, const int y) const {
  renderer.fillRect(x, y, width, height, true);
  if (width > 2 && height > 2) {
    renderer.fillRect(x + 1, y + 1, width - 2, height - 2, false);
  }
}

void ImageBlock::render(GfxRenderer& renderer, const int x, const int y) {
  // The font-prewarm scan pass only accumulates glyphs; an image contributes
  // none, and its DirectPixelWriter output bypasses the renderer's scan-mode
  // suppression, so it would otherwise do a full (discarded) cache render every
  // page view. Skip it here. The image still draws in the real BW/grayscale
  // passes; on first view this just moves the one-time decode to the BW pass.
  FontCacheManager* fcm = renderer.getFontCacheManager();
  if (fcm && fcm->isScanning()) return;

  LOG_DBG("IMG", "Rendering image at %d,%d: %s (%dx%d)", x, y, imagePath.c_str(), width, height);

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // Bounds check render position using logical screen dimensions
  if (x < 0 || y < 0 || x + width > screenWidth || y + height > screenHeight) {
    LOG_ERR("IMG", "Invalid render position: (%d,%d) size (%dx%d) screen (%dx%d)", x, y, width, height, screenWidth,
            screenHeight);
    return;
  }

  // Tiled grayscale (#2190): skip the whole image when it doesn't touch the
  // active band. The per-pixel writer already clips off-band pixels, but without
  // this each of the ~7 bands per plane re-ran the full cache load / pixel walk
  // and discarded the result — the dominant cost of AA on image pages. The check
  // is orientation-aware and returns true when no strip is active, so the BW
  // pass and non-tiled controllers render the image exactly as before.
  if (!renderer.glyphIntersectsStrip(x, y, x + width - 1, y + height - 1)) {
    return;
  }

  if (imageFailedThisRender(imagePath)) {
    renderPlaceholder(renderer, x, y);
    return;
  }

  // Try to render from cache first
  std::string cachePath = getCachePath(imagePath);
  if (renderFromCache(renderer, cachePath, x, y, width, height)) {
    renderer.preserveImagePolarity(x, y, width, height);
    return;  // Successfully rendered from cache
  }

  // The build only header-probed the image for dimensions; pull the actual
  // file out of the book now, on first visit to the page.
  if (!srcPath.empty() && extractFn && !Storage.exists(imagePath.c_str())) {
    LOG_DBG("IMG", "Lazy-extracting %s -> %s", srcPath.c_str(), imagePath.c_str());
    if (!extractFn(extractCtx, srcPath.c_str(), imagePath.c_str())) {
      LOG_ERR("IMG", "Lazy extraction failed: %s", srcPath.c_str());
    }
  }

  // No cache - need to decode the image
  // Check if image file exists
  HalFile file;
  if (!Storage.openFileForRead("IMG", imagePath, file)) {
    LOG_ERR("IMG", "Image file not found: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }
  size_t fileSize = file.size();
  file.close();

  if (fileSize == 0) {
    LOG_ERR("IMG", "Image file is empty: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  LOG_DBG("IMG", "Decoding and caching: %s", imagePath.c_str());

  RenderConfig config;
  config.x = x;
  config.y = y;
  config.maxWidth = width;
  config.maxHeight = height;
  config.useGrayscale = true;
  config.useDithering = true;
  config.performanceMode = false;
  config.useExactDimensions = true;  // Use pre-calculated dimensions to avoid rounding mismatches
  config.cachePath = cachePath;      // Enable caching during decode

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(imagePath);
  if (!decoder) {
    LOG_ERR("IMG", "No decoder found for image: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  LOG_DBG("IMG", "Using %s decoder", decoder->getFormatName());

  bool success = decoder->decodeToFramebuffer(imagePath, renderer, config);
  if (!success) {
    LOG_ERR("IMG", "Failed to decode image: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  renderer.preserveImagePolarity(x, y, width, height);
  LOG_DBG("IMG", "Decode successful");
}
