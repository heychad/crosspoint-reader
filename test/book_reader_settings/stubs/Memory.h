#pragma once
#include <memory>
#include <new>
#include <utility>
inline bool failNextTestAllocation = false;
template <typename T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (std::exchange(failNextTestAllocation, false)) return {};
  using Element = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) Element[count]());
}
