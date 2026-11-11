#pragma once

#include <HalStorage.h>

// Stops a streaming extraction at a write boundary without owning the sink.
class InterruptiblePrint final : public Print {
  Print& sink_;
  bool (*cancel_)(void*);
  void* context_;
  bool cancelled_ = false;

 public:
  InterruptiblePrint(Print& sink, bool (*cancel)(void*), void* context)
      : sink_(sink), cancel_(cancel), context_(context) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t size) override {
    if (cancelled_ || (cancel_ && cancel_(context_))) {
      cancelled_ = true;
      return 0;
    }
    return sink_.write(bytes, size);
  }
  bool cancelled() const { return cancelled_; }
};
