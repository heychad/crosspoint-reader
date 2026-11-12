#pragma once

class HalFile;
class Print;

class BmpToBmpConverter {
 public:
  // Crops or fits a BMP to the requested bounds. Native gray levels are preserved;
  // other color sources get 2-bit Atkinson dithering. 1-bit sources stay 1-bit.
  static bool bmpFileToBmpStream(HalFile& bmpFile, Print& bmpOut, int targetWidth, int targetHeight, bool crop,
                                 bool originalThresholds);
};
