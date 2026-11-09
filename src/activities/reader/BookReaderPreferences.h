#pragma once

#include <string>

#include "personalization/BookReaderSettings.h"

// Main-task book preference store. JSON work happens at settings boundaries, never per page.
class BookReaderPreferences {
 public:
  bool load(const std::string& cachePath, const std::string& bookPath, BookReaderSettings& settings);
  bool save(const BookReaderSettings& settings);
  static bool prepareMove(const std::string& cachePath, const std::string& bookPath);

 private:
  std::string destination;
  std::string source;
  bool writable = true;
  bool legacyLoaded = false;
  BookReaderSettings legacySettings;
};
