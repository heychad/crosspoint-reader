#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "StageRequest.h"

// Prototype only: no network caller or publication. Partial stages are kept.
class StageWriter {
 public:
  static constexpr size_t BUFFER_BYTES = 4096;
  static constexpr uint64_t MAX_BYTES = syncstage::MAX_STAGE_BYTES;
  explicit StageWriter(HalStorage& storage) : storage_(storage) {}
  bool begin(const char* name, uint64_t expectedSize);
  bool write(const uint8_t* data, size_t size);
  bool finish();
  void abort();
  bool takeResult();

 private:
  bool flush();
  enum class State { Idle, Receiving, Failed, Complete };
  HalStorage& storage_;
  State state_ = State::Idle;
  HalFile file_;
  std::unique_ptr<uint8_t[]> buffer_;
  size_t buffered_ = 0;
  uint64_t received_ = 0;
  uint64_t expected_ = 0;
};
