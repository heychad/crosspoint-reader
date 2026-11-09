#pragma once

#include <atomic>
#include <cstdint>

namespace uiscale {
enum class Value : uint8_t { Small, Large };
inline std::atomic<uint8_t> active{static_cast<uint8_t>(Value::Large)};
inline uint8_t current() { return active.load(std::memory_order_acquire); }
inline void publish(uint8_t value) { active.store(value, std::memory_order_release); }
}  // namespace uiscale
