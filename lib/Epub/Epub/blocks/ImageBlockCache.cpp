#include <Serialization.h>

#include <new>
#include <utility>

#include "CacheString.h"
#include "ImageBlock.h"

ImageBlock::ImageBlock(std::string imagePath, std::string srcPath, int16_t width, int16_t height)
    : imagePath(std::move(imagePath)), srcPath(std::move(srcPath)), width(width), height(height) {}

bool ImageBlock::serialize(HalFile& file) {
  if (!serialization::tryWriteString(file, imagePath)) return false;
  if (!serialization::tryWriteString(file, srcPath)) return false;
  if (!serialization::tryWritePod(file, width)) return false;
  if (!serialization::tryWritePod(file, height)) return false;
  return true;
}

std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile& file) {
  std::string path;
  std::string source;
  int16_t width, height;
  if (!epubcache::readString(file, path) || !epubcache::readString(file, source) ||
      !serialization::tryReadPod(file, width) || !serialization::tryReadPod(file, height))
    return nullptr;
  return std::unique_ptr<ImageBlock>(new (std::nothrow) ImageBlock(std::move(path), std::move(source), width, height));
}
